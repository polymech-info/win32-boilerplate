# Duplicates report

**Generated (UTC):** 2026-04-28T15:01:07Z

## Summary

| Field | Value |
| --- | --- |
| `meta_empty_files` | 0 |
| `n_groups` | 1 |
| `ok` | true |
| `scanned` | 4 |
| `skipped_fingerprint` | 0 |
| `skipped_meta` | 0 |

## Options (effective)

```json
{
  "detailed_report": true,
  "fingerprint_same_size_only": false,
  "llm_api_key_set": true,
  "llm_base_url": "",
  "llm_model": "anthropic/claude-sonnet-4.6",
  "llm_router": "openrouter",
  "llm_timeout_ms": 60000,
  "max_hamming": 0,
  "meta_json_compare_prompt": "",
  "meta_json_implicit_generate": false,
  "meta_json_llm_compare": true,
  "meta_json_min_similarity": 7,
  "meta_prompt": "",
  "min_group_size": 2,
  "mode": "meta",
  "recursive": true,
  "use_exif": false,
  "use_json": true,
  "use_md": false
}
```

## Input specs

- `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta`

## Grouped result

### meta — key `llm_json:524a7acc7d46b182c89f57951cc7a70120f607b699a33d99372be4d568e41ebb` (3 paths)

- `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\DSC07036.JPG`
- `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\DSC07036_replace_the_background_with_a_white_stud.JPG`
- `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\DSC07121.JPG`

## Per-image duplicate map (`duplicate_map`)

For automation (delete, move, review): each `by_path` key is a file; `peers` is the list of other images in the same *emitted* group with `pairwise` size / dHash / Hamming details (see JSON).

Mode: `meta`

| File | # peers | group_key (short) |
| --- | ---: | --- |
| `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\DSC07036.JPG` | 2 | `llm_json:524a7acc7d46…` |
| `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\DSC07036_replace_the_background_with_a_white_stud.JPG` | 2 | `llm_json:524a7acc7d46…` |
| `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\DSC07121.JPG` | 2 | `llm_json:524a7acc7d46…` |

## Mode-specific diagnostics

### Meta JSON LLM compare (OpenRouter / chat provider)

| A | B | similarity (0–10) | notes |
| --- | --- | ---: | --- |
| `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\DSC07036.JPG` | `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\DSC07036_replace_the_background_with_a_white_stud.JPG` | 8 | Both images show the same industrial machine (press/bending brake with black frame, red hydraulic component, and caster wheels). JSON_B appears to be a background-replaced version of the same machine from JSON_A, consistent with the filename suffix. The core subject is identical; differences stem from background removal and slightly different descriptive focus. |
| `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\DSC07036.JPG` | `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\DSC07121.JPG` | 4 | Both show industrial machines on wheeled frames in a workshop with white walls, but A features a bending/press machine with rectangular panels leaning against the wall, while B shows a larger automated machine with control panels and pneumatic hoses — likely different machines or very different angles/configurations of the same space. |
| `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\DSC07036.JPG` | `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\logo.png` | 0 | Completely unrelated: A is an industrial metalworking machine in a workshop; B is a yellow abstract logo on white background. |
| `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\DSC07036_replace_the_background_with_a_white_stud.JPG` | `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\DSC07121.JPG` | 7 | Both depict the same type of large industrial heat press/hydraulic machine on a wheeled black metal frame with control panels and pneumatic tubing/blue hoses and a red hydraulic component. Likely the same machine photographed from different angles or distances, with one having a replaced white background. Key shared details: caster wheels, black frame, hydraulic red component, pneumatic hoses, control panel enclosure. |
| `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\DSC07036_replace_the_background_with_a_white_stud.JPG` | `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\logo.png` | 0 | Completely unrelated: one is an industrial heat press machine, the other is an abstract yellow logo. |
| `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\DSC07121.JPG` | `C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp\tests\assets\duplicate\meta\logo.png` | 0 | Completely unrelated: A is an industrial machine in a workshop; B is an abstract yellow logo on white background. |

## Full JSON (machine-readable)

This section is the same structure as `--report-json` / `result.report`.

```json
{
  "action_log": [],
  "candidates": [
    "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036.JPG",
    "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036_replace_the_background_with_a_white_stud.JPG",
    "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07121.JPG",
    "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\logo.png"
  ],
  "duplicate_map": {
    "by_path": {
      "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036.JPG": {
        "group_key": "llm_json:524a7acc7d46b182c89f57951cc7a70120f607b699a33d99372be4d568e41ebb",
        "method": "meta",
        "peer_count": 2,
        "peers": [
          {
            "pairwise": {
              "match_basis": "llm_openrouter_json_sidecar_fields",
              "method": "meta cmp:json",
              "notes": "Both images show the same industrial machine (press/bending brake with black frame, red hydraulic component, and caster wheels). JSON_B appears to be a background-replaced version of the same machine from JSON_A, consistent with the filename suffix. The core subject is identical; differences stem from background removal and slightly different descriptive focus.",
              "similarity_0_10": 8
            },
            "path": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036_replace_the_background_with_a_white_stud.JPG"
          },
          {
            "pairwise": {
              "match_basis": "llm_openrouter_json_sidecar_fields",
              "method": "meta cmp:json",
              "notes": "Both show industrial machines on wheeled frames in a workshop with white walls, but A features a bending/press machine with rectangular panels leaning against the wall, while B shows a larger automated machine with control panels and pneumatic hoses — likely different machines or very different angles/configurations of the same space.",
              "similarity_0_10": 4
            },
            "path": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07121.JPG"
          }
        ]
      },
      "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036_replace_the_background_with_a_white_stud.JPG": {
        "group_key": "llm_json:524a7acc7d46b182c89f57951cc7a70120f607b699a33d99372be4d568e41ebb",
        "method": "meta",
        "peer_count": 2,
        "peers": [
          {
            "pairwise": {
              "match_basis": "llm_openrouter_json_sidecar_fields",
              "method": "meta cmp:json",
              "notes": "Both images show the same industrial machine (press/bending brake with black frame, red hydraulic component, and caster wheels). JSON_B appears to be a background-replaced version of the same machine from JSON_A, consistent with the filename suffix. The core subject is identical; differences stem from background removal and slightly different descriptive focus.",
              "similarity_0_10": 8
            },
            "path": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036.JPG"
          },
          {
            "pairwise": {
              "match_basis": "llm_openrouter_json_sidecar_fields",
              "method": "meta cmp:json",
              "notes": "Both depict the same type of large industrial heat press/hydraulic machine on a wheeled black metal frame with control panels and pneumatic tubing/blue hoses and a red hydraulic component. Likely the same machine photographed from different angles or distances, with one having a replaced white background. Key shared details: caster wheels, black frame, hydraulic red component, pneumatic hoses, control panel enclosure.",
              "similarity_0_10": 7
            },
            "path": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07121.JPG"
          }
        ]
      },
      "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07121.JPG": {
        "group_key": "llm_json:524a7acc7d46b182c89f57951cc7a70120f607b699a33d99372be4d568e41ebb",
        "method": "meta",
        "peer_count": 2,
        "peers": [
          {
            "pairwise": {
              "match_basis": "llm_openrouter_json_sidecar_fields",
              "method": "meta cmp:json",
              "notes": "Both show industrial machines on wheeled frames in a workshop with white walls, but A features a bending/press machine with rectangular panels leaning against the wall, while B shows a larger automated machine with control panels and pneumatic hoses — likely different machines or very different angles/configurations of the same space.",
              "similarity_0_10": 4
            },
            "path": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036.JPG"
          },
          {
            "pairwise": {
              "match_basis": "llm_openrouter_json_sidecar_fields",
              "method": "meta cmp:json",
              "notes": "Both depict the same type of large industrial heat press/hydraulic machine on a wheeled black metal frame with control panels and pneumatic tubing/blue hoses and a red hydraulic component. Likely the same machine photographed from different angles or distances, with one having a replaced white background. Key shared details: caster wheels, black frame, hydraulic red component, pneumatic hoses, control panel enclosure.",
              "similarity_0_10": 7
            },
            "path": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036_replace_the_background_with_a_white_stud.JPG"
          }
        ]
      }
    },
    "mode": "meta",
    "version": 1
  },
  "format_version": 2,
  "generated_utc": "2026-04-28T15:01:07Z",
  "groups": [
    {
      "count": 3,
      "key": "llm_json:524a7acc7d46b182c89f57951cc7a70120f607b699a33d99372be4d568e41ebb",
      "method": "meta",
      "paths": [
        "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036.JPG",
        "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036_replace_the_background_with_a_white_stud.JPG",
        "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07121.JPG"
      ]
    }
  ],
  "inputs": [
    "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta"
  ],
  "kind": "pm-image.duplicates",
  "meta_json_compare": {
    "min_similarity": 7,
    "model": "anthropic/claude-sonnet-4.6",
    "pairs": [
      {
        "notes": "Both images show the same industrial machine (press/bending brake with black frame, red hydraulic component, and caster wheels). JSON_B appears to be a background-replaced version of the same machine from JSON_A, consistent with the filename suffix. The core subject is identical; differences stem from background removal and slightly different descriptive focus.",
        "path_a": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036.JPG",
        "path_b": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036_replace_the_background_with_a_white_stud.JPG",
        "similarity_0_10": 8
      },
      {
        "notes": "Both show industrial machines on wheeled frames in a workshop with white walls, but A features a bending/press machine with rectangular panels leaning against the wall, while B shows a larger automated machine with control panels and pneumatic hoses — likely different machines or very different angles/configurations of the same space.",
        "path_a": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036.JPG",
        "path_b": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07121.JPG",
        "similarity_0_10": 4
      },
      {
        "notes": "Completely unrelated: A is an industrial metalworking machine in a workshop; B is a yellow abstract logo on white background.",
        "path_a": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036.JPG",
        "path_b": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\logo.png",
        "similarity_0_10": 0
      },
      {
        "notes": "Both depict the same type of large industrial heat press/hydraulic machine on a wheeled black metal frame with control panels and pneumatic tubing/blue hoses and a red hydraulic component. Likely the same machine photographed from different angles or distances, with one having a replaced white background. Key shared details: caster wheels, black frame, hydraulic red component, pneumatic hoses, control panel enclosure.",
        "path_a": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036_replace_the_background_with_a_white_stud.JPG",
        "path_b": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07121.JPG",
        "similarity_0_10": 7
      },
      {
        "notes": "Completely unrelated: one is an industrial heat press machine, the other is an abstract yellow logo.",
        "path_a": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07036_replace_the_background_with_a_white_stud.JPG",
        "path_b": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\logo.png",
        "similarity_0_10": 0
      },
      {
        "notes": "Completely unrelated: A is an industrial machine in a workshop; B is an abstract yellow logo on white background.",
        "path_a": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\DSC07121.JPG",
        "path_b": "C:\\Users\\zx\\Desktop\\polymech\\polymech-mono\\packages\\media\\cpp\\tests\\assets\\duplicate\\meta\\logo.png",
        "similarity_0_10": 0
      }
    ],
    "router": "openrouter"
  },
  "mode": "meta",
  "options": {
    "detailed_report": true,
    "fingerprint_same_size_only": false,
    "llm_api_key_set": true,
    "llm_base_url": "",
    "llm_model": "anthropic/claude-sonnet-4.6",
    "llm_router": "openrouter",
    "llm_timeout_ms": 60000,
    "max_hamming": 0,
    "meta_json_compare_prompt": "",
    "meta_json_implicit_generate": false,
    "meta_json_llm_compare": true,
    "meta_json_min_similarity": 7,
    "meta_prompt": "",
    "min_group_size": 2,
    "mode": "meta",
    "recursive": true,
    "use_exif": false,
    "use_json": true,
    "use_md": false
  },
  "summary": {
    "meta_empty_files": 0,
    "n_groups": 1,
    "ok": true,
    "scanned": 4,
    "skipped_fingerprint": 0,
    "skipped_meta": 0
  },
  "tool": "duplicates"
}
```

