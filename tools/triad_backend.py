"""Canonical Robust backend API for application integration.

WeakReceiver acquires the public Helper/DATA bank. PracticalTrackingReceiver
consumes a declared post-acquisition bank. NativeStream supplies the shared
bounded source/basis service; none is a competing standalone modem.

Numerically intensive validation must use triad_abbreviated. Historical
receivers and protocol revisions belong in Git tags and separate releases.
"""
from triad_continuous import NativeStream
from triad_weak_receiver import WeakReceiver
from triad_tracking_receiver import PracticalTrackingReceiver

from triad_audio_mapping import MappedCaptureReader, mapped_phase, rescale_coordinates
from triad_stream_frontend import StreamingAudioDDC
from triad_stream_scheduler import StreamingWindowScheduler, WindowRequest
from triad_template_bank import generate_template_bank
from triad_duration_model import plan_from_native
from triad_continuous import (tracking_preflight, tracking_source_basis,
                              disciplined_capture_extent, endpoint_decision, receiver_noise_variance)

# The native full-audio bridge is triad_audio_stream; its mapped-v1 pipe uses
# MappedCaptureReader. Audio transport does not add another receiver.
__all__ = ["NativeStream", "WeakReceiver", "PracticalTrackingReceiver",
           "MappedCaptureReader", "mapped_phase", "rescale_coordinates",
           "StreamingAudioDDC", "StreamingWindowScheduler", "WindowRequest",
           "generate_template_bank", "plan_from_native", "tracking_preflight",
           "tracking_source_basis", "disciplined_capture_extent", "endpoint_decision",
           "receiver_noise_variance"]
