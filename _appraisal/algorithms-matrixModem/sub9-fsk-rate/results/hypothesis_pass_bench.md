# One pass over the messages for many hypotheses, CPU

threads 12; 24-bit block, all 2^24 messages enumerated in every timed pass; synthetic tables with a
planted message that has to win for every hypothesis. "Per hypothesis" is one enumeration of the
messages for each hypothesis, timed on 8 hypotheses and multiplied; "one pass" enumerates them once.

## Real entries

Per hypothesis: 0.0860 s for all messages (5.13 ns per message and hypothesis); planted found in 8 of 8.

| Hypotheses | One pass, all messages | Per message and hypothesis | Per hypothesis would take | One pass is faster by | Planted found for |
| ---: | ---: | ---: | ---: | ---: | --- |
| 7 | 0.365 s | 3.11 ns | 0.60 s | 1.6 | 7 of 7 |
| 56 | 0.804 s | 0.86 ns | 4.82 s | 6.0 | 56 of 56 |
| 350 | 4.509 s | 0.77 ns | 30.10 s | 6.7 | 350 of 350 |

## Complex entries

Per hypothesis: 0.1261 s for all messages (7.52 ns per message and hypothesis); planted found in 8 of 8.

| Hypotheses | One pass, all messages | Per message and hypothesis | Per hypothesis would take | One pass is faster by | Planted found for |
| ---: | ---: | ---: | ---: | ---: | --- |
| 7 | 0.652 s | 5.55 ns | 0.88 s | 1.4 | 7 of 7 |
| 56 | 1.822 s | 1.94 ns | 7.06 s | 3.9 | 56 of 56 |
| 350 | 9.717 s | 1.65 ns | 44.14 s | 4.5 | 350 of 350 |

Table size in the one pass: 32 symbols x 256 values x hypotheses x 2 bytes (x 2 for complex):
0.1 MB for 7 real hypotheses, 0.9 MB for 56, 5.7 MB for 350, twice that for complex entries.
