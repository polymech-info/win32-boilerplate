# AR4_SG1_gear.scad — AI Tool Demo Workflow

This README documents a demo workflow for the selected OpenSCAD file:

```text
AR4_SG1_gear.scad
```

The purpose is to demonstrate how the AI tool can assist with CAD-adjacent media generation, product visualization, video creation, voice notification, and CAD format conversion automation.

> Demo purpose only: this file outlines the AI-assisted process rather than serving as formal engineering documentation.

---

## 1. Prompt used to create the SCAD file

Example prompt for generating the OpenSCAD model in the first place:

```text
Create an OpenSCAD file for a mechanical gear component named AR4_SG1_gear.
The model should be suitable for a robotic or mechanical assembly demo.
Use parametric dimensions where possible, include gear teeth, central bore,
mounting/relief features, and clean structure with comments so it can be edited later.
Output valid .scad code only.
```

Expected output:

```text
AR4_SG1_gear.scad
```

The resulting `.scad` file can be opened in OpenSCAD, rendered, exported, or used as source geometry for downstream conversion.

---

## 2. Conversion to a fancy product rendering using the image creation tool

The AI image creation tool can be used to create a polished product-style rendering from the CAD concept.

Example prompt:

```text
Create a fancy product rendering of the AR4_SG1 gear component.
Make it look like a premium machined mechanical part on a clean studio background.
Use metallic material, soft reflections, dramatic lighting, shallow depth of field,
and a modern engineering product-shot style.
```

Tool used:

```text
image_create
```

Example generated output from this demo:

```text
AR4_SG1_gear_fancy_render.png
```

This step is intended to show how a raw technical CAD source can be turned into a visually appealing concept/product image for presentations, documentation, or marketing mockups.

---

## 3. Image-to-video product clip using the video creation tool, with speaker notification

After creating the rendered product image, the AI video tool can animate it into a short product clip.

Example prompt:

```text
Create a short polished product video of the AR4_SG1 gear.
Show a slow cinematic camera orbit, premium studio lighting,
subtle reflections on the metal surface, and a clean engineering presentation style.
Notify me over the speaker when done.
```

Tool used:

```text
create_video
```

Speaker notification tool used after completion:

```text
speak
```

Example spoken notification:

```text
Your short product video is done.
```

Example generated output from this demo:

```text
video_create_a_short_polished_product_video_of.mp4
```

This demonstrates an end-to-end AI media workflow:

1. Start from a CAD/SCAD source file.
2. Generate a polished product render.
3. Animate the render into a short product video.
4. Announce completion using text-to-speech.

---

## 4. Script to find FreeCAD and convert OpenSCAD to STEP

A helper script was created to convert `.scad` files into `.step` files using FreeCAD.

Main script:

```text
scad_to_step.py
```

Wrapper scripts:

```text
scad_to_step.bat
scad_to_step.cmd
scad_to_step.sh
```

The script is designed to be cross-platform:

- Windows
- macOS
- Linux

It supports:

- Relative input and output paths
- Absolute input and output paths
- Auto-detection of FreeCAD / FreeCADCmd
- Optional OpenSCAD path configuration
- Single-file conversion
- Folder-based conversion workflows, depending on script arguments

Example Windows usage:

```bat
scad_to_step.bat AR4_SG1_gear.scad AR4_SG1_gear.step
```

or:

```bat
scad_to_step.cmd AR4_SG1_gear.scad AR4_SG1_gear.step
```

Example macOS/Linux usage:

```sh
./scad_to_step.sh AR4_SG1_gear.scad AR4_SG1_gear.step
```

If needed on Unix-like systems:

```sh
chmod +x scad_to_step.sh
```

Example with explicit FreeCAD path on Windows:

```bat
scad_to_step.bat AR4_SG1_gear.scad AR4_SG1_gear.step --freecad "C:\Program Files\FreeCAD 0.21\bin\FreeCADCmd.exe"
```

Expected STEP output:

```text
AR4_SG1_gear.step
```

This demonstrates how the AI assistant can move beyond media generation and help automate engineering file conversion workflows.

---

## Demo workflow summary

```text
Prompt → OpenSCAD file → Fancy render → Product video → Speaker notification → STEP conversion script
```

Associated tools demonstrated:

| Stage | Tool / Capability |
|---|---|
| Generate CAD source prompt | AI code/content generation |
| Read selected `.scad` file | File reading |
| Create product image | `image_create` |
| Create product video | `create_video` |
| Announce completion | `speak` |
| Create conversion scripts | File writing |
| Test conversion locally | Shell/run tool + FreeCAD |

The goal of this demo is to show an AI-assisted workflow for combining CAD source files, visual rendering, video generation, voice feedback, and engineering format conversion in one integrated toolchain.
