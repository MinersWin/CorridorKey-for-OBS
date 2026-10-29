<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Production catalogue inventory

Revision 3: `5d88114f169cb082cda238426af47feffb9724af` in
`mantelmoritz/CorridorKey-ONNX-Graphs`. Counts are bytes. The graph/JSON pairs
were downloaded anonymously and checked on 29 October 2026. Full machine-readable
pins and verification notes are in [validation-w09b.json](validation-w09b.json).

| Package | Download bytes | Bound bytes | Manifest SHA-256 |
| --- | ---: | ---: | --- |
| ck2-facecam | 3292123815 | 2332083292 | `f1e57bfd769efda18641698be4a99aed9d63420eb8a1cda60629b77783a7deb0` |
| ck2-balanced | 3296058594 | 3139268972 | `46e3d4b8ed17039c25e39147f1886ae823b98f367f63f0a21a46aaf1e6cef789` |
| refiner-v12 | 230644 | 374080 | `8274d32f8fa56ec2235501825dbd7984db3f16d414e1aa33a027710382fb344e` |

Publisher checkpoints are shared as verified cache blobs across packages. The
download counts include all required checkpoints; Facecam and Balanced overlap.

| Source | Bytes | SHA-256 |
| --- | ---: | --- |
| ck2 | 2661698720 | `c43596de98cc8866d577c8a9f9ea239acdf3f7a130ff24e25db4375a50590436` |
| stats | 18864 | `924783ceddd293fd470126332ce328166faea6c4c8d2f36402d5b95187166af3` |
| moge | 351063528 | `be74d978c6a69cb467c057aa8b736e4b3a8eb9b2ec41622edd6b26c756573e3b` |
| refiner | 94336 | `377d7c60ab7f6a5691e9977a491bcea397ade1421bb6087f2bc79ad3a9ce315d` |
| rvm | 270086372 | `8821ba9f5d983b9a3746bb5fe86b9c2ea834fe9985ab7b02438b917438bf2ede` |

Full source revisions are in [sources-v1.json](../../data/models/sources-v1.json).
RVM retains publisher MD5 `ZJb6kj8+tasiJbVeplXu7Q==`; installation uses SHA-256.

| Graph / metadata | Bytes | SHA-256 |
| --- | ---: | --- |
| `graphs/moge_256x512.onnx` | 1102163 | `df5d7ede17cb0eb9d720d63af25ec114d0e4dbff3f30b2be6796c88e071b32aa` |
| `graphs/moge_256x512.json` | 52215 | `7c999b23cc8d1774e28441a644b4d6dd32c47f6afbecb98527f38a1db8a135cf` |
| `graphs/rvm_256x512.onnx` | 1899632 | `d4577d07e75a8d6ed4071064212ffd6e4729e41657818a61d9c248553898e9bb` |
| `graphs/rvm_256x512.json` | 108779 | `be9eb7ed49afe7ac6cd8eac99e0f1ce9105a3c432494639a7ffbda86bd38e691` |
| `graphs/stage1nomask_256x512.onnx` | 5952369 | `352379960c6961d9da10bdf6115ded4047f93f795bedf4027a60f8051ab1d6da` |
| `graphs/stage1nomask_256x512.json` | 141173 | `ca9674b05772305c9cdbd34fd178fb4119fc7f9d6ef06bc430528ba318b38316` |
| `graphs/moge_512x512.onnx` | 1102163 | `ac9dec954278bab3a47f15bac8a930631c51c79c871cdb675cf4fbdb6c32c63e` |
| `graphs/moge_512x512.json` | 52216 | `ba1ea4df74eca62798fdc4af4b39d43b6e1afa4f0ffdea2f3943ccfc7a094446` |
| `graphs/rvm_512x512.onnx` | 1899636 | `7187c65e4039892a0c8e545f367d46ba6dbe78680ec15c85fc72e6de0f226bb3` |
| `graphs/rvm_512x512.json` | 108780 | `f2d80c1d1d0d84483a1831a2af412c43478003de1b722a61c81118bcdbe50d8c` |
| `graphs/stage1nomask_512x512.onnx` | 6488004 | `498c697246d1a089f7b6c9c3b1980392e52b12194029a691854098fbee264bb5` |
| `graphs/stage1nomask_512x512.json` | 141173 | `80ef9507231b096655e7b4ca4959146345fbee6e59fdf7760e9ce973e97466a2` |
| `graphs/stage2nomask_512x512.onnx` | 3334405 | `d67141a67c4cd75062045525cf2088ff9aa0e697e9094a96c1cdea38c979d5b5` |
| `graphs/stage2nomask_512x512.json` | 64733 | `bedd565daea725a97d5b7c78dc7d3cc8902b4763d1d7a88361aa0d202ab30404` |
| `graphs/refiner_b256.onnx` | 30443 | `06bd74527b07f2ccbbbbf92e110f51f296d9140d86f48ebb0a3067b4acfc5108` |
| `graphs/refiner_b256.json` | 3634 | `bc6c70df41b000e06c1975e309a6840da4deffed0f015c08860adcda711bc9d1` |
| `graphs/refiner_b384.onnx` | 30443 | `82698ff62ba402a25ce7f4f28b0cbf27cb81dac3014f8ed320b4fa5fb6f4e823` |
| `graphs/refiner_b384.json` | 3634 | `7b68782047a90499a9f9248a78521ba5354953c6df48b35487f63fe2a9ceb9e4` |
| `graphs/refiner_b512.onnx` | 30443 | `784a5825ecab22f9040ec95ae992b972928475bb7cdd9849e8a85c639dc61a45` |
| `graphs/refiner_b512.json` | 3634 | `2e10f73fbbc744303ed19dc6b58d4bac2c97d4e10fd2d36910fef6f374c209df` |
| `graphs/refiner_b768.onnx` | 30443 | `739c5704be317d596ab8e283e68af8d9d41fe734ad591d017d5e3873aefd1224` |
| `graphs/refiner_b768.json` | 3634 | `94e608108e9786d12673dc38de229b45facfb87d297a87f9726ed86cbef82e98` |

| Locally bound output | Bytes | Expected SHA-256 |
| --- | ---: | --- |
| `graphs/moge_256x512.weights` | 351049804 | `90a6078688cf8c53b1447a066c05c7bf58d91c0818924a199469e588b9048863` |
| `graphs/rvm_256x512.weights` | 132893696 | `21b2f868e5b51297cc4803fa246219f808ea1cfc0bb1fb49c8e8918133709192` |
| `graphs/stage1nomask_256x512.weights` | 1848139792 | `c0120dff1ad83d6f2417d33b75f5324c2f82f285072dc01f383c2abef00178e1` |
| `graphs/moge_512x512.weights` | 351049804 | `90a6078688cf8c53b1447a066c05c7bf58d91c0818924a199469e588b9048863` |
| `graphs/rvm_512x512.weights` | 132893696 | `21b2f868e5b51297cc4803fa246219f808ea1cfc0bb1fb49c8e8918133709192` |
| `graphs/stage1nomask_512x512.weights` | 1848139792 | `c0120dff1ad83d6f2417d33b75f5324c2f82f285072dc01f383c2abef00178e1` |
| `graphs/stage2nomask_512x512.weights` | 807185680 | `ab218724cc9428b1273ccce58d081ccd023ba7bed4e716600aea587fcbb32f5d` |
| `graphs/refiner_b256.weights` | 93520 | `05e61731b9d37c31dd870197bdd08783eab446abfd5a6afa6ad6db1d94f3656b` |
| `graphs/refiner_b384.weights` | 93520 | `05e61731b9d37c31dd870197bdd08783eab446abfd5a6afa6ad6db1d94f3656b` |
| `graphs/refiner_b512.weights` | 93520 | `05e61731b9d37c31dd870197bdd08783eab446abfd5a6afa6ad6db1d94f3656b` |
| `graphs/refiner_b768.weights` | 93520 | `05e61731b9d37c31dd870197bdd08783eab446abfd5a6afa6ad6db1d94f3656b` |

Bound-output pins were computed from the exporter's local files. The first-run
installation verifies the native binder's results against those pins before
publishing the package. No model or graph payload is stored in Git.

## CK1 and MODNet (revision 4)

Graph release `082f53c75d275c10b1ddf3be2cb72c5ca559f30b` adds separate green and blue CK1 packages and a MODNet hint. Each includes the 256, 384 and 512 square grids; CK1 defaults to 384. All checkpoint and graph bytes were checked anonymously against the pinned publisher listings. JSON bytes matched the HF Git blob IDs before SHA-256 was computed.

| Package | Download bytes | Bound bytes | Manifest SHA-256 |
| --- | ---: | ---: | --- |
| ck1-green | 405579584 | 1196433024 | `0aa19052839e2e1a40102777757f8b173a34438a311dffecfeca44f541a9f1f8` |
| ck1-blue | 405577406 | 1196433024 | `bd72990e5465120a6537fb9c7dc0b94e48bb79665de597e180ac34c8f4df8312` |
| modnet | 37655230 | 78138144 | `e5618de7af40dab7d49e1976bb6b1917e50f28f6a64f5e9d555e7b28e94e29ff` |

Full revisions, graph/metadata byte counts and checkpoint hashes are in [validation-w14-artifacts.json](validation-w14-artifacts.json). Per-grid bound-output hashes and interfaces are in the package manifests. All nine locally bound weight files match the exporter byte for byte. The MODNet acceptance record contains both the pinned LICENSE and NOTICE. CK1 uses its published v1 additional terms, separately from the graph licence. GPU parity and test conditions are covered in [CK1 validation](ck1-pipeline.md).
