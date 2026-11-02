# EPG replay fixtures

Copied from SI4684-FMDAB-Receiver revision
`df0c8bc9eef4c4598742fd462216675d1e885d81` (GPL-3.0, same as this project).

`epg_demo.h` preserves historical MUX 11B directory, F64C SI and F629 PI
data. Tests use a virtual 2024 clock; these are not current programme listings.
`synthetic_logo_1_groups_v2.b64` preserves the reference synthetic PI/SI/logo
group stream; decoded SHA-256:
`52e6f81385ed6edb6d2246aa889e0833762fdb9a5d881648699f1c3c7126e6c8`.

Both are host-only fixtures, absent from firmware. The full large RAW corpus
is not included and its 1087-programme oracle was not executed in this stage.
