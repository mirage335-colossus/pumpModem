#!/usr/bin/env python3
"""Device-free boundary tests for the bounded triad audio pipe bridge."""
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

BINARY = sys.argv[1] if len(sys.argv) == 2 else None
sys.argv = sys.argv[:1]


class AudioStreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if BINARY is None:
            raise RuntimeError("usage: test_triad_audio_stream.py PATH_TO_BINARY")
        cls.binary = BINARY

    def run_copy(self, data, frames, *args):
        return subprocess.run([self.binary, "--mode", "copy", "--frames", str(frames), *args],
                              input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)

    def test_bounded_copy_preserves_float_bytes_and_receipt(self):
        values = [0., -0.125, 0.25, 0.5, -1.]
        payload = struct.pack("<5f", *values)
        result = self.run_copy(payload, len(values), "--sample-rate", "48000")
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        self.assertEqual(result.stdout, payload)
        receipt = json.loads(result.stderr)
        self.assertEqual(receipt["schema"], "triad-audio-stream-v1")
        self.assertTrue(receipt["complete"])
        self.assertEqual(receipt["requested_frames"], len(values))
        self.assertEqual(receipt["completed_frames"], len(values))
        self.assertFalse(receipt['clock_discipline']['enabled'])

    def test_clock_discipline_requires_a_dated_device_and_valid_limits(self):
        for flags in (('--clock-discipline','continuous'), ('--clock-max-ppm','2000'),
                      ('--clock-phase-seconds','nan'), ('--clock-discipline','unknown')):
            result = self.run_copy(b'', 0, *flags)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, b'')
        # A total bound equal to the default phase cap is valid; decimal ppm
        # conversion must not put it one floating-point ULP below that cap.
        result = self.run_copy(b'', 0, '--clock-max-ppm', '200')
        self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_mapped_capture_format_is_explicit_and_device_free_modes_stay_raw(self):
        for mode, value in (('copy','mapped-v1'),('play','mapped-v1'),
                            ('copy','adc-v1'),('play','adc-v1'),('capture','unknown')):
            result=subprocess.run([self.binary,'--mode',mode,'--frames','0','--capture-format',value],
                capture_output=True,timeout=5)
            self.assertNotEqual(result.returncode,0)
            self.assertEqual(result.stdout,b'')
        result=self.run_copy(b'',0,'--clock-discipline','auto')
        self.assertEqual(result.returncode,0,result.stderr.decode())
        receipt=json.loads(result.stderr)
        self.assertFalse(receipt['clock_discipline']['enabled'])
        self.assertEqual(receipt['capture_format'],'raw')
        help_result=subprocess.run([self.binary,'--help'],capture_output=True,timeout=5)
        self.assertEqual(help_result.returncode,0)
        self.assertIn(b'auto (default) follows UTC for play/capture',help_result.stdout)
        self.assertIn(b'--clock-phase-max-ppm 200',help_result.stdout)
        self.assertIn(b'raw|mapped-v1|adc-v1',help_result.stdout)

    def test_native_adc_mode_requires_active_clock_before_device_open(self):
        result = subprocess.run([self.binary, '--mode', 'capture', '--frames', '1',
            '--capture-format', 'adc-v1', '--clock-discipline', 'off'],
            capture_output=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b'')
        self.assertIn(b'adc-v1 requires active continuous clock discipline', result.stderr)

    def test_short_stream_does_not_claim_completion(self):
        payload = struct.pack("<2f", .25, -.5)
        result = self.run_copy(payload, 3)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"")
        self.assertIn(b"before declared frame count", result.stderr)
        self.assertIn(b'"complete":false', result.stderr)

    def test_partial_float_frame_is_rejected(self):
        result = self.run_copy(b"\x00\x00\x00", 1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"inside a float32 frame", result.stderr)

    def test_nonfinite_pcm_is_rejected_before_output(self):
        result = self.run_copy(struct.pack('<f', float('nan')), 1)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b'')
        self.assertIn(b'nonfinite PCM', result.stderr)

    def test_unimplemented_capture_channel_is_rejected_without_device(self):
        result = subprocess.run([self.binary, '--mode', 'capture', '--frames', '1',
            '--channels', 'right'], capture_output=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'left logical mono', result.stderr)

    def test_large_declared_runtime_has_no_duration_guard(self):
        # Immediate EOF proves the frame count is parsed and accepted without
        # allocating a duration-sized buffer or waiting for the requested time.
        result = self.run_copy(b"", 86400 * 48_000, "--sample-rate", "48000")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"before declared frame count", result.stderr)

    def test_noise_options_are_only_allowed_for_final_playback(self):
        result = self.run_copy(b"", 0, "--background-noise-bits", "10")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"copy mode does not model", result.stderr)

    def test_cli_rejects_missing_count_and_invalid_audio_mode(self):
        missing = subprocess.run([self.binary, "--mode", "copy"], capture_output=True, timeout=5)
        bad_mode = subprocess.run([self.binary, "--mode", "invalid", "--frames", "0"],
                                  capture_output=True, timeout=5)
        self.assertNotEqual(missing.returncode, 0)
        self.assertNotEqual(bad_mode.returncode, 0)


class NativeAdcMockTests(unittest.TestCase):
    """Compile the actual pipe adapter with a tiny, device-free provider.

    This exercises monitor/callback ordering, framing and completion, not ADC
    hardware or the production ASRC's numerical behavior. No audio is opened.
    """
    @classmethod
    def setUpClass(cls):
        compiler = shlex.split(os.environ.get('CXX', 'c++'))
        if not compiler or shutil.which(compiler[0]) is None:
            raise unittest.SkipTest('C++ compiler unavailable: native ADC mock untested')
        cls.temp = tempfile.TemporaryDirectory(prefix='triad-adc-mock-')
        cls.addClassCleanup(cls.temp.cleanup)
        root = Path(__file__).resolve().parents[1]
        source = Path(cls.temp.name) / 'mock.cpp'
        cls.binary = str(Path(cls.temp.name) / 'mock')
        source.write_text('#define main triad_stream_entry\n#include ' +
                          json.dumps(str(root / 'tools/triad-audio-stream.cpp')) + r'''
#undef main
namespace datapump::audio {
void playback(std::uint32_t, const std::string&, const PlaybackCallback&,
              std::stop_token, StreamFormatCallback, ChannelMode, Options) {
    throw std::runtime_error("mock playback is unavailable");
}
void capture(std::uint32_t rate, const std::string& device, const CaptureCallback& callback,
             std::stop_token, StreamFormatCallback on_format, Options options) {
    StreamFormat format;
    format.logical_rate=rate; format.hardware_rate=rate+(device=="rate-mismatch"?1:0);
    format.hardware_channels=2; format.logical_channels=1; format.selected_capture_channel=0;
    if(device!="missing-format")on_format(format);
    if(!options.continuous_clock || !options.continuous_clock_report)
        throw std::runtime_error("ADC mock requires monitor and active clock pipeline");
    if(!options.capture_monitor) {
        std::uint64_t first=0;
        for(int chunk=0;chunk<10;++chunk) {
            std::vector<float> logical(17);
            std::vector<double> offsets(17);
            for(std::size_t i=0;i<logical.size();++i) {
                logical[i]=static_cast<float>(first+i)/64.f;offsets[i]=i+.5;
            }
            if(options.capture_sample_map)options.capture_sample_map({first,first+4,rate,offsets});
            options.continuous_clock_report({});
            if(!callback(logical))return;
            first+=logical.size();
        }
        return;
    }
    std::uint64_t origin=0;
    for(int chunk=0;chunk<10;++chunk) {
        if(chunk==1 && device=="discontinuity")options.capture_discontinuity();
        if(chunk==1 && device=="early-stop")return;
        std::vector<float> native(chunk?5000:17);
        for(std::size_t i=0;i<native.size();++i) {
            const auto index=origin+i;
            native[i]=static_cast<float>(static_cast<int>(index%101)-50)/64.f;
            if(index==9)native[i]=.9995f;
            if(index==17)native[i]=-1.f;
        }
        if(device=="nonfinite")native[6]=std::numeric_limits<float>::quiet_NaN();
        options.capture_monitor(native,rate+(device=="monitor-rate-mismatch"?1:0));
        origin+=native.size();
        // Distinct post-ASRC samples and counts must never escape via adc-v1.
        ClockDisciplineReport report;report.capture=true;report.observations=chunk+1;
        report.native_frame=origin;report.applied_correction=.0002;
        if(device!="missing-clock-report")options.continuous_clock_report(report);
        if(device=="no-post-callback")continue;
        const std::array<float,11> converted{.123f};
        if(!callback(converted))return;
    }
}
}
int main(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--mock-queue-check") {
        Queue queue;queue.cap=8;
        CaptureBlock full;full.pcm.resize(8);
        if(!enqueue_capture_block(queue,std::move(full)) || queue.frames!=8)return 1;
        CaptureBlock extra;extra.pcm.resize(1);
        if(enqueue_capture_block(queue,extra) || !queue.failed || queue.frames!=8 || queue.blocks.size()!=1)return 2;
        queue.error="first writer failure";
        if(enqueue_capture_block(queue,extra) || queue.error!="first writer failure")return 3;
        return 0;
    }
    return triad_stream_entry(argc,argv);
}
''')
        build = subprocess.run([*compiler, '-std=c++20', '-O0', '-pthread',
            '-I', str(root / 'include'), str(source), '-o', cls.binary],
            capture_output=True, timeout=25)
        if build.returncode:
            raise RuntimeError(build.stderr.decode(errors='replace'))

    def run_adc(self, frames=7003, device='mock'):
        return subprocess.run([self.binary, '--mode', 'capture', '--frames', str(frames),
            '--capture-format', 'adc-v1', '--sample-rate', '48000', '--device', device],
            capture_output=True, timeout=5)

    def test_identity_packets_are_raw_bounded_contiguous_and_exactly_trimmed(self):
        result = self.run_adc()
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        receipt = json.loads(result.stderr)
        self.assertTrue(receipt['complete'])
        self.assertEqual(receipt['completed_frames'], 7003)
        self.assertEqual(receipt['observed_native_frames'], 10017)
        self.assertEqual(receipt['unretained_native_tail_frames'], 3014)
        self.assertEqual(receipt['capture_observation'], 'pre-asrc-native-mono')
        self.assertEqual(receipt['discarded_post_asrc_frames'], 33)
        self.assertTrue(receipt['clock_discipline']['enabled'])
        self.assertEqual(receipt['clock_discipline']['reports'], 3)
        self.assertEqual(receipt['clipped_samples'], 2)
        self.assertEqual(receipt['capture_stream_format'], dict(logical_rate=48000,
            hardware_rate=48000, logical_channels=1, hardware_channels=2, selected_capture_channel=0))
        position = 0
        pcm = []
        sizes = []
        while position < len(result.stdout):
            magic, version, count, first, origin, rate, reserved = struct.unpack_from(
                '<8sIIQQII', result.stdout, position)
            self.assertEqual((magic, version, rate, reserved), (b'TRIADMAP', 1, 48000, 0))
            self.assertGreater(count, 0)
            self.assertLessEqual(count, 4096)
            self.assertEqual((first, origin), (len(pcm), len(pcm)))
            position += 40
            offsets = struct.unpack_from('<' + str(count) + 'd', result.stdout, position)
            self.assertEqual(offsets, tuple(range(count)))
            position += count * 8
            pcm.extend(struct.unpack_from('<' + str(count) + 'f', result.stdout, position))
            position += count * 4
            sizes.append(count)
        self.assertEqual(sizes, [17, 4096, 904, 1986])
        self.assertEqual(len(pcm), 7003)
        expected = [(i % 101 - 50) / 64 for i in range(7003)]
        expected[9] = struct.unpack('<f', struct.pack('<f', .9995))[0]
        expected[17] = -1.
        self.assertEqual(pcm, expected)

    def test_existing_raw_and_mapped_output_keep_logical_pcm(self):
        for capture_format in ('raw', 'mapped-v1'):
            with self.subTest(capture_format=capture_format):
                result = subprocess.run([self.binary, '--mode', 'capture', '--frames', '35',
                    '--capture-format', capture_format], capture_output=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr.decode())
                receipt = json.loads(result.stderr)
                self.assertEqual(receipt['capture_observation'], 'post-asrc-logical-mono')
                self.assertEqual(receipt['completed_frames'], 35)
                if capture_format == 'raw':
                    self.assertEqual(result.stdout, struct.pack('<35f', *(i / 64 for i in range(35))))
                else:
                    position = first = 0
                    for count in (17, 17, 1):
                        header = struct.unpack_from('<8sIIQQII', result.stdout, position)
                        self.assertEqual(header, (b'TRIADMAP', 1, count, first, first + 4, 48000, 0))
                        position += 40
                        self.assertEqual(struct.unpack_from('<' + str(count) + 'd', result.stdout, position),
                                         tuple(i + .5 for i in range(count)))
                        position += count * 8
                        self.assertEqual(struct.unpack_from('<' + str(count) + 'f', result.stdout, position),
                                         tuple(i / 64 for i in range(first, first + count)))
                        position += count * 4
                        first += count
                    self.assertEqual(position, len(result.stdout))

    def test_bad_rate_or_missing_format_fails_before_pcm(self):
        for device in ('rate-mismatch', 'monitor-rate-mismatch', 'missing-format'):
            with self.subTest(device=device):
                result = self.run_adc(device=device)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, b'')
                self.assertIn(b'"complete":false', result.stderr)

    def test_discontinuity_truncation_and_missing_pipeline_witness_never_complete(self):
        for device in ('discontinuity', 'early-stop', 'nonfinite',
                       'no-post-callback', 'missing-clock-report'):
            with self.subTest(device=device):
                result = self.run_adc(device=device)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b'"complete":false', result.stderr)
                self.assertNotIn(b'"complete":true', result.stderr)

    def test_bounded_queue_failure_preserves_first_error(self):
        result = subprocess.run([self.binary, '--mock-queue-check'], capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr.decode())


if __name__ == "__main__":
    unittest.main()
