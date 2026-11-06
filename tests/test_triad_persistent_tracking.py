#!/usr/bin/env python3
"""Independent covariance and normalized-density checks for timing persistence."""
from pathlib import Path
import math
import sys
import unittest

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_gaussian_state import GaussianPrior, GaussianStateFilter
from triad_incremental_receiver import IncrementalReceiver
from triad_tracking_receiver import TrackingReceiver, PersistentTrackingReceiver, PracticalTrackingReceiver
from triad_timing_forward import SparseTimingForward, TimingState


class PersistentTrackingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        torch.backends.cuda.matmul.allow_tf32 = False
        torch.set_float32_matmul_precision('highest')

    @staticmethod
    def prior(*, moving=False, models=2, states=None, cap=64):
        return SparseTimingForward(states or [TimingState(0, 0, 0)], model_count=models,
            model_probabilities=[.3,.7] if models==2 else None,
            delay_step_seconds=.0137, rate_step_ppm=50,
            maximum_rate_ppm=50 if moving else 0,
            maximum_acceleration_ppm_per_second=100 if moving else 0,
            maximum_states=cap, maximum_proposals=cap)

    @staticmethod
    def provider(states, anchor, lo, hi, origin, offsets):
        t=(origin+offsets)/16
        out=np.zeros((3,len(states),1,hi-lo),complex)
        for j,state in enumerate(states):
            u=t+state.delay_index*.0137+state.rate_index*50e-6*(t-float(anchor))
            out[1,j,0]=(.7+.3j)*np.exp(2j*np.pi*.3*u)+.4*np.exp(-2j*np.pi*.9*u)
            out[2,j,0]=(.2-.8j)*np.exp(2j*np.pi*.6*u)-.3j*np.exp(-2j*np.pi*1.1*u)
        return out

    def make(self, cls=PersistentTrackingReceiver, *, samples=64, segment=16,
             prior=None, provider=None, coherences=(.6,math.inf), **kw):
        return cls(sample_rate=16,samples=samples,block_samples=8,segment_samples=segment,
            basis_provider=provider or self.provider,timing_forward=prior or self.prior(),
            required_bands=[[-7,7]],noise_variance=1.7,stationary_power=(.8,),
            coherence_seconds=coherences,precision='fp64',projection_mode='valid-rows',
            maximum_input_chunk_samples=128,memory_limit_bytes=32*1024**2,**kw)

    def test_single_state_matches_full_persistent_receiver_and_chunks(self):
        rng=np.random.default_rng(9811); values=rng.normal(size=64)+1j*rng.normal(size=64)
        t=np.arange(64,dtype=float); mapping=t+.002*np.sin(t/7)
        direct=IncrementalReceiver(sample_rate=16,samples=64,block_samples=8,
            basis=lambda lo,hi:self.provider((TimingState(0,0,0),),0,lo,hi,0,mapping[lo:hi]),
            block_center_seconds=lambda lo,hi:(mapping[lo]+mapping[hi-1])/32,
            required_bands=[[-7,7]],noise_variance=1.7,stationary_power=(.8,),
            coherence_seconds=(.6,math.inf),precision='fp64',projection_mode='valid-rows')
        direct.push(0,values); direct.finish(); unmixed=direct.unmixed_log_evidence()[:,1:,0]
        expected=np.logaddexp(np.log(.3)+unmixed[0],np.log(.7)+unmixed[1])
        answers=[]
        for chunk in (3,19,64):
            rx=self.make(require_physical_map=True)
            for lo in range(0,64,chunk):
                hi=min(lo+chunk,64); origin=math.floor(mapping[lo])
                rx.push(lo,values[lo:hi],physical_origin=origin,physical_offsets=mapping[lo:hi]-origin)
            result=rx.finish(); answers.append(result['bit_scores'])
            self.assertTrue(result['complete_observation']);self.assertIsNone(result['admission'])
            self.assertEqual(result['work_counts']['candidate_block_updates'],8*3*2)
            self.assertEqual(result['work_counts']['basis_rows'],64*2)
            self.assertEqual(result['segments'],4)
            self.assertGreater(result['channel_state_bytes'],0)
        np.testing.assert_allclose(answers,np.broadcast_to(expected,(3,2)),rtol=0,atol=8e-13)

    def test_static_channel_partition_and_erased_segments(self):
        rng=np.random.default_rng(9812); values=rng.normal(size=53)+1j*rng.normal(size=53)
        valid=np.ones(53,dtype=np.uint8);valid[16:32]=0
        answers=[]
        for segment in (8,16,53):
            rx=self.make(samples=53,segment=segment,coherences=(math.inf,),prior=self.prior(models=1))
            rx.push(0,values,valid);r=rx.finish();answers.append(r['bit_scores'])
            self.assertEqual(r['erased_rows'],16)
            self.assertEqual(r['observed_rows'],37)
        np.testing.assert_allclose(answers,np.broadcast_to(answers[0],(3,2)),rtol=0,atol=5e-13)

    def test_finite_channel_unequal_terminal_segment_and_large_mapped_origin(self):
        base=2**54+177
        n=np.arange(53,dtype=float);mapping=n+.031*np.sin(n/8)
        rng=np.random.default_rng(9817);values=rng.normal(size=53)+1j*rng.normal(size=53)
        def provider(states,anchor,lo,hi,origin,offsets):
            return self.provider(states,0,lo,hi,0,(origin-base)+offsets)
        rx=self.make(samples=53,provider=provider,require_physical_map=True)
        for lo in range(0,53,11):
            hi=min(lo+11,53);origin=base+math.floor(mapping[lo])
            rx.push(lo,values[lo:hi],physical_origin=origin,physical_offsets=mapping[lo:hi]-(origin-base))
        result=rx.finish()
        boundaries=[(i,min(i+8,53)) for i in range(0,53,8)]
        times=np.array([(mapping[lo]+mapping[hi-1])/32 for lo,hi in boundaries])
        basis=self.provider((TimingState(0,0,0),),0,0,53,0,mapping)[1:,0,0]
        expected=[]
        for bit in (0,1):
            scores=[]
            for coh in (.6,math.inf):
                a=np.zeros((53,len(boundaries)),complex)
                for j,(lo,hi) in enumerate(boundaries):a[lo:hi,j]=basis[bit,lo:hi]
                h=.8*np.ones((len(times),len(times))) if math.isinf(coh) else .8*np.exp(-abs(times[:,None]-times[None,:])/coh)
                covariance=np.eye(53)+a@h@a.conj().T/1.7;z=values/math.sqrt(1.7)
                scores.append(float((np.vdot(z,z)-np.vdot(z,np.linalg.solve(covariance,z))).real-np.linalg.slogdet(covariance)[1]))
            expected.append(np.logaddexp(math.log(.3)+scores[0],math.log(.7)+scores[1]))
        np.testing.assert_allclose(result['bit_scores'],expected,rtol=0,atol=7e-13)

    def test_two_segment_branching_matches_dense_path_covariances(self):
        rng=np.random.default_rng(9813); values=rng.normal(size=32)+1j*rng.normal(size=32)
        rx=self.make(samples=32,prior=self.prior(moving=True),coherences=(.6,math.inf))
        rx.push(0,values);r=rx.finish()
        initial=TimingState(0,0,0); center0=7.5/16;center1=23.5/16
        first=self.provider((initial,),center0,0,16,0,np.arange(16))[1:,0,0]
        expected=np.zeros((2,2))
        # Exhaustive proper-complex full covariance for every two-segment path.
        for state,p in self.prior(moving=True).transitions(initial,1):
            second=self.provider((state,),center1,16,32,16,np.arange(16))[1:,0,0]
            for m,coh in enumerate((.6,math.inf)):
                for bit in (0,1):
                    a=np.concatenate((first[bit],second[bit])); columns=np.zeros((32,4),complex)
                    centers=np.array([3.5,11.5,19.5,27.5])/16
                    for b in range(4):columns[b*8:(b+1)*8,b]=a[b*8:(b+1)*8]
                    h=.8*np.ones((4,4)) if math.isinf(coh) else .8*np.exp(-abs(centers[:,None]-centers[None,:])/coh)
                    covariance=np.eye(32)+columns@h@columns.conj().T/1.7
                    z=values/math.sqrt(1.7)
                    loge=(np.vdot(z,z)-np.vdot(z,np.linalg.solve(covariance,z))).real-np.linalg.slogdet(covariance)[1]
                    expected[m,bit]+=p*math.exp(loge)
        expected=np.log((np.array([.3,.7])[:,None]*expected).sum(0))
        np.testing.assert_allclose(r['bit_scores'],expected,rtol=0,atol=4e-12)
        self.assertGreater(r['work_counts']['transition_edges'],0)

    def test_gauge_transport_preserves_integrated_phase_at_delay_rounding(self):
        # Source envelope is exactly flat; fractional delay grid is deliberately
        # not an integer number of audio-center cycles.  Each public rate path
        # has a continuous integrated audio phase, despite lattice rounding.
        fs=16;center=1725.3
        def provider(states,anchor,lo,hi,origin,offsets):
            t=(origin+offsets)/fs;out=np.zeros((3,len(states),1,hi-lo),complex)
            for j,state in enumerate(states):
                delta=state.delay_index*.0137+state.rate_index*50e-6*(t-float(anchor))
                out[1,j,0]=np.exp(2j*np.pi*center*delta)
                out[2,j,0]=.3j*np.exp(2j*np.pi*center*delta)
            return out
        rng=np.random.default_rng(9814);values=1.1+(.04*rng.normal(size=32)+.04j*rng.normal(size=32))
        rx=self.make(samples=32,provider=provider,prior=self.prior(moving=True,models=1),
            coherences=(math.inf,),audio_center_hz=center)
        rx.push(0,values);result=rx.finish();expected=np.zeros(2)
        for state,p in self.prior(moving=True,models=1).transitions(TimingState(0,0,0),1.):
            t=np.arange(16,32)/fs
            # Mean phase transports d*=.5*r*dt, not rounded delay_index.
            delta=.5*state.rate_index*50e-6+state.rate_index*50e-6*(t-23.5/fs)
            for bit,coefficient in enumerate((1.,.3j)):
                a=np.concatenate((np.full(16,coefficient),coefficient*np.exp(2j*np.pi*center*delta)))
                energy=np.vdot(a,a).real/1.7;corr=np.vdot(a,values)/1.7
                expected[bit]+=p*math.exp(.8*abs(corr)**2/(1+.8*energy)-math.log1p(.8*energy))
        np.testing.assert_allclose(result['bit_scores'],np.log(expected),rtol=0,atol=3e-12)
        no_gauge=self.make(samples=32,provider=provider,prior=self.prior(moving=True,models=1),coherences=(math.inf,))
        no_gauge.push(0,values);wrong=no_gauge.finish()
        self.assertGreater(abs(result['bit_scores'][0]-wrong['bit_scores'][0]),.01)

        # The shorter terminal segment changes the midpoint geometry. Physical
        # carrier phase must remain continuous at 15.5/Fs, not at the midpoint
        # between the unequal segment anchors.
        short_values=values[:21]
        short=self.make(samples=21,provider=provider,prior=self.prior(moving=True,models=1),
            coherences=(math.inf,),audio_center_hz=center)
        short.push(0,short_values);short_result=short.finish();expected=np.zeros(2)
        dt=(18.-7.5)/fs
        for state,p in self.prior(moving=True,models=1).transitions(TimingState(0,0,0),dt):
            t=np.arange(16,21)/fs;delta=state.rate_index*50e-6*(t-15.5/fs)
            for bit,coefficient in enumerate((1.,.3j)):
                a=np.concatenate((np.full(16,coefficient),coefficient*np.exp(2j*np.pi*center*delta)))
                energy=np.vdot(a,a).real/1.7;corr=np.vdot(a,short_values)/1.7
                expected[bit]+=p*math.exp(.8*abs(corr)**2/(1+.8*energy)-math.log1p(.8*energy))
        np.testing.assert_allclose(short_result['bit_scores'],np.log(expected),rtol=0,atol=3e-12)

    def test_merging_preserves_normalized_predictive_noise_density(self):
        # Integrate the next predictive LR under its exact proper-complex null
        # after a genuine multi-history merge. Gauss-Hermite quadrature gives an
        # independent deterministic integral, not a noisy null pass count.
        rx=self.make(samples=64,segment=16,prior=self.prior(moving=True),coherences=(.6,math.inf))
        rx.push(0,np.full(32,.5+.4j))
        rx._predict_timing(1.)
        self.assertGreater(rx.merged_channel_components,0)
        from numpy.polynomial.hermite import hermgauss
        x,w=hermgauss(40); y=(x[:,None]+1j*x[None,:]).reshape(-1)
        quadrature=(w[:,None]*w[None,:]/math.pi).reshape(-1)
        for mean,cov in rx._next_posteriors:
            for bit in (1,2):
                for j in range(mean.shape[1]):
                    mu=complex(mean[bit,j,0]);variance=float(cov[bit,j,0,0].real)
                    # Predict one unit-noise scalar with A=.2+.1j.
                    a=.2+.1j; signal_mean=a*mu;signal_var=abs(a)**2*variance
                    loglr=abs(y)**2-abs(y-signal_mean)**2/(1+signal_var)-math.log1p(signal_var)
                    self.assertAlmostEqual(float(np.dot(quadrature,np.exp(loglr))),1.,places=12)

    def test_seed_validation_and_incomplete_prefix(self):
        f=GaussianStateFilter(GaussianPrior((1.,),math.inf))
        with self.assertRaisesRegex(ValueError,'positive semidefinite'):
            f.initialize_posterior(torch.zeros(1,dtype=torch.complex128),torch.tensor([[-1.]],dtype=torch.complex128))
        f=GaussianStateFilter(GaussianPrior((1.,),math.inf))
        f.initialize_posterior(torch.tensor([.4j],dtype=torch.complex128),torch.tensor([[.7]],dtype=torch.complex128))
        self.assertEqual(float(f.scores()),0.)
        with self.assertRaisesRegex(ValueError,'unused'):
            f.initialize_posterior(*f.posterior())
        rx=self.make();rx.push(0,np.ones(17));r=rx.finish()
        self.assertFalse(r['complete_observation']);self.assertIsNone(r['admission'])
        self.assertEqual(r['processed_rows'],16)

    def test_weak_three_carrier_control_retains_evidence_across_real_branches(self):
        # Cheap analytic DATA control. Its complete observation contains little
        # energy in any segment; renewing a nuisance gain loses useful evidence.
        # This is not a native-waveform or communication-reliability claim.
        count=2048;variance=96.;rng=np.random.default_rng(78211)
        coefficients=((.7+.2j,.4-.3j,.2+.4j),(.3-.5j,-.2+.6j,.4+.1j))
        frequencies=((-0.04,0.,.04),(-.035,.005,.045))
        def provider(states,anchor,lo,hi,origin,offsets):
            t=origin+offsets;out=np.zeros((3,len(states),1,hi-lo),complex)
            for j,state in enumerate(states):
                u=t+state.delay_index*.0137+state.rate_index*50e-6*(t-float(anchor))
                for bit in (0,1):
                    out[bit+1,j,0]=sum(c*np.exp(2j*np.pi*f*u)
                        for c,f in zip(coefficients[bit],frequencies[bit]))
            return out
        truth=provider((TimingState(0,0,0),),0,0,count,0,np.arange(count))[1,0,0]
        values=truth+math.sqrt(variance/2)*(rng.normal(size=count)+1j*rng.normal(size=count))
        reports=[]
        for cls in (TrackingReceiver,PersistentTrackingReceiver):
            rx=cls(sample_rate=1,samples=count,block_samples=16,segment_samples=16,
                basis_provider=provider,timing_forward=self.prior(moving=True,models=1),
                required_bands=[[-.49,.49]],noise_variance=variance,stationary_power=(1.,),
                coherence_seconds=(math.inf,),precision='fp64',projection_mode='valid-rows',
                maximum_input_chunk_samples=count,memory_limit_bytes=64*1024**2)
            rx.push(0,values);reports.append(rx.finish())
        old,new=reports
        self.assertLess(float(np.logaddexp(*old['bit_scores'])-math.log(2)),math.log(10000))
        self.assertGreater(float(np.logaddexp(*new['bit_scores'])-math.log(2)),math.log(10000))
        self.assertEqual(int(np.argmax(new['bit_scores'])),0)
        self.assertGreater(new['work_counts']['merged_channel_components'],0)
        self.assertEqual(new['timing']['retained_initial_path_probability'],old['timing']['retained_initial_path_probability'])


class PracticalTrackingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    def test_fixed_prior_mixture_preserves_initial_affine_map_and_all_priors(self):
        base=2**54+177;fs=16.;center=17.253
        states=[TimingState(0,-1,-1),TimingState(0,2,1),TimingState(1,0,0)]
        probabilities=[.2,.3,.5];model_probabilities=[.3,.7]
        initial_anchor=[]
        def prior():
            return SparseTimingForward(states,probabilities,model_count=2,
                model_probabilities=model_probabilities,delay_step_seconds=.0137,
                rate_step_ppm=50,maximum_rate_ppm=50,maximum_acceleration_ppm_per_second=100,
                maximum_states=64,maximum_proposals=64)
        def provider(states,anchor,lo,hi,origin,offsets):
            t=((origin-base)+offsets)/fs
            relative_anchor=float(np.longdouble(anchor)-np.longdouble(base)/fs)
            out=np.zeros((3,len(states),1,hi-lo),complex)
            for j,state in enumerate(states):
                delta=state.delay_index*.0137+state.rate_index*50e-6*(t-relative_anchor)
                u=t+delta
                out[1,j,0]=np.exp(2j*np.pi*(.3+.07*state.group)*u)*np.exp(2j*np.pi*center*delta)
                out[2,j,0]=(.4+.3j)*np.exp(-2j*np.pi*(.6+.03*state.group)*u)*np.exp(2j*np.pi*center*delta)
            return out
        n=np.arange(64,dtype=float);mapping=n+.021*np.sin(n/7)
        rng=np.random.default_rng(9818);values=rng.normal(size=64)+1j*rng.normal(size=64)
        arguments=dict(sample_rate=fs,samples=64,block_samples=8,segment_samples=16,
            basis_provider=provider,required_bands=[[-7,7]],noise_variance=1.7,
            stationary_power=(.8,),coherence_seconds=(.6,math.inf),precision='fp64',
            projection_mode='valid-rows',maximum_input_chunk_samples=128,
            memory_limit_bytes=64*1024**2,require_physical_map=True,audio_center_hz=center)
        answers=[]
        for chunk in (5,64):
            rx=PracticalTrackingReceiver(timing_forward=prior(),**arguments)
            for lo in range(0,64,chunk):
                hi=min(lo+chunk,64);origin=base+math.floor(mapping[lo])
                rx.push(lo,values[lo:hi],physical_origin=origin,physical_offsets=mapping[lo:hi]-(origin-base))
            result=rx.finish();answers.append(result['bit_scores'])
            a=np.asarray(result['lanes']['adaptive']['bit_scores'])
            b=np.asarray(result['lanes']['initial_affine']['bit_scores'])
            np.testing.assert_allclose(result['bit_scores'],np.logaddexp(a,b)-math.log(2),rtol=0,atol=1e-14)
            self.assertTrue(np.all(np.asarray(result['bit_scores'])>=b-math.log(2)-1e-14))
            self.assertEqual(result['working_bytes_estimate'],rx.adaptive.owned_bytes_estimate+rx.nominal.owned_bytes_estimate)
            self.assertLessEqual(result['working_bytes_estimate'],64*1024**2)
            self.assertEqual(result['work_counts']['basis_rows'],result['lanes']['adaptive']['work_counts']['basis_rows']+64*2*len(states))
            self.assertEqual(result['lanes']['initial_affine']['work_counts']['transition_edges'],3*len(states))
            self.assertEqual(result['maximum_segment_cpu_seconds'],result['lanes']['adaptive']['maximum_segment_cpu_seconds']+result['lanes']['initial_affine']['maximum_segment_cpu_seconds'])
            anchor=np.longdouble(base)/fs+np.longdouble((mapping[0]+mapping[15])/32)
            direct=IncrementalReceiver(sample_rate=fs,samples=64,block_samples=8,
                basis=lambda lo,hi:provider(tuple(sorted(states)),anchor,lo,hi,base,mapping[lo:hi]),
                block_center_seconds=lambda lo,hi:(mapping[lo]+mapping[hi-1])/32,
                required_bands=[[-7,7]],noise_variance=1.7,stationary_power=(.8,),
                coherence_seconds=(.6,math.inf),precision='fp64',projection_mode='valid-rows',candidate_count=len(states))
            direct.push(0,values);direct.finish();columns=direct.unmixed_log_evidence()[:,1:]
            # Keep public group and model weights explicit in this independent
            # full-observation reference; do not replace them by uniform priors.
            expected=np.logaddexp.reduce(columns+np.log(probabilities)[None,None,:],axis=-1)
            expected=np.logaddexp.reduce(expected+np.log(model_probabilities)[:,None],axis=0)
            np.testing.assert_allclose(b,expected,rtol=0,atol=3e-12)
            self.assertTrue(result['complete_observation']);self.assertIsNone(result['admission'])
        np.testing.assert_allclose(answers[0],answers[1],rtol=0,atol=1e-12)

    def test_combined_memory_rejects_before_basis_and_failure_never_completes(self):
        calls=[]
        def provider(*args):calls.append(1);raise ValueError('deliberate source failure')
        common=dict(sample_rate=16,samples=64,block_samples=8,segment_samples=16,
            basis_provider=provider,required_bands=[[-7,7]],stationary_power=(1.,),
            coherence_seconds=(math.inf,),precision='fp64',projection_mode='valid-rows',
            maximum_input_chunk_samples=128)
        def prior():return SparseTimingForward([TimingState(0,0,0)],maximum_rate_ppm=0)
        with self.assertRaises(MemoryError):
            PracticalTrackingReceiver(timing_forward=prior(),memory_limit_bytes=1000,**common)
        self.assertEqual(calls,[])
        rx=PracticalTrackingReceiver(timing_forward=prior(),memory_limit_bytes=16*1024**2,**common)
        self.assertFalse(rx.finish()['complete_observation'])
        with self.assertRaisesRegex(ValueError,'source failure'):rx.push(0,np.ones(16))
        with self.assertRaisesRegex(RuntimeError,'failure'):rx.push(0,np.ones(16))
        failed=rx.finish();self.assertEqual(failed['status'],'failed')
        self.assertFalse(failed['complete_observation']);self.assertIsNone(failed['admission'])
        self.assertIsNone(failed['bit_scores'])

    def test_erased_first_segment_keeps_original_nonzero_affine_anchor(self):
        states=[TimingState(0,2,1)];seen=[]
        def provider(states,anchor,lo,hi,origin,offsets):
            seen.append(float(anchor))
            return PersistentTrackingTests.provider(states,anchor,lo,hi,origin,offsets)
        prior=SparseTimingForward(states,model_count=1,delay_step_seconds=.0137,
            rate_step_ppm=50,maximum_rate_ppm=2000,maximum_acceleration_ppm_per_second=0,
            maximum_states=8,maximum_proposals=8)
        rx=PracticalTrackingReceiver(sample_rate=16,samples=48,block_samples=8,segment_samples=16,
            basis_provider=provider,timing_forward=prior,required_bands=[[-7,7]],
            stationary_power=(.8,),coherence_seconds=(math.inf,),precision='fp64',
            projection_mode='valid-rows',maximum_input_chunk_samples=64,memory_limit_bytes=32*1024**2)
        valid=np.ones(48,dtype=np.uint8);valid[:16]=0
        values=np.exp(.2j*np.arange(48))
        rx.push(0,values,valid);result=rx.finish()
        self.assertEqual(float(result['lanes']['initial_affine']['initial_affine_anchor_seconds']),7.5/16)
        direct=IncrementalReceiver(sample_rate=16,samples=48,block_samples=8,
            basis=lambda lo,hi:PersistentTrackingTests.provider(states,7.5/16,lo,hi,0,np.arange(lo,hi)),
            required_bands=[[-7,7]],stationary_power=(.8,),coherence_seconds=(math.inf,),
            precision='fp64',projection_mode='valid-rows',candidate_count=1)
        direct.push(0,values,valid);expected=direct.finish()['bit_scores']
        np.testing.assert_allclose(result['lanes']['initial_affine']['bit_scores'],expected,rtol=0,atol=3e-13)
        self.assertEqual(rx.nominal.timing.rate_index_bound,1)

    def test_nonbinary_public_rate_grid_keeps_admitted_initial_indices(self):
        for step,index,bound in ((.7,3,3.),(1.1,15,20.),(.1,43,5.)):
            forward=SparseTimingForward([TimingState(0,0,index)],rate_step_ppm=step,
                maximum_rate_ppm=bound,maximum_states=1,maximum_proposals=1)
            rx=PracticalTrackingReceiver(sample_rate=16,samples=8,block_samples=8,segment_samples=8,
                basis_provider=PersistentTrackingTests.provider,timing_forward=forward,
                required_bands=[[-7,7]],stationary_power=(1.,),coherence_seconds=(math.inf,),
                precision='fp64',projection_mode='valid-rows',maximum_input_chunk_samples=8,
                memory_limit_bytes=16*1024**2)
            self.assertGreaterEqual(rx.nominal.timing.rate_index_bound,index)


if __name__=='__main__':unittest.main()
