# Search arithmetic policy

This page records retired single-carrier search controls. They are not part of
the canonical Robust stack and are not current GUI controls. See
[Robust architecture and status](robust-current.md).

The former `--search-arithmetic` option and its Default, FP32-minimum and
FP64-force receiver policies have been removed with that receiver. Their
source-specific behavior and GPU comparisons survive only as
[historical precision research](gpu-precision-controls.md), not current CLI,
GUI, or implementation guidance. No current Robust GPU execution path is
qualified.
