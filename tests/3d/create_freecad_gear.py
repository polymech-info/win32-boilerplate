#!/usr/bin/env python3
"""
create_freecad_gear.py - Create AR4_SG1_gear.FCStd with FreeCAD.

Native FreeCAD/Python recreation of AR4_SG1_gear.scad defaults. No OpenSCAD required.
Run with:
  "C:\\Users\\mc007\\AppData\\Local\\Programs\\FreeCAD 1.1\\bin\\freecadcmd.exe" create_freecad_gear.py
"""
from __future__ import annotations

import math
import os
import sys

import FreeCAD as App  # type: ignore
import Part  # type: ignore

teeth_count = 18
gear_thickness = 6.0
tooth_tip_radius = 10.541
tooth_root_radius = 8.4431757035
bore_diameter = 3.0
shaft_diameter = bore_diameter
shaft_length = 18.0
shaft_z_offset = 0.0
tooth_tip_half_angle_degrees = 3.82
tooth_base_half_angle_degrees = 8.25
tooth_tip_arc_steps = 4
tooth_root_arc_steps = 2
include_shaft = True


def polar_point(radius: float, angle_degrees: float) -> App.Vector:
    a = math.radians(angle_degrees)
    return App.Vector(radius * math.cos(a), radius * math.sin(a), 0.0)


def arc_points(radius: float, start_angle_degrees: float, end_angle_degrees: float, segment_count: int) -> list[App.Vector]:
    return [polar_point(radius, start_angle_degrees + (end_angle_degrees - start_angle_degrees) * i / segment_count) for i in range(segment_count + 1)]


def tooth_points(tooth_index: int) -> list[App.Vector]:
    pitch = 360.0 / teeth_count
    centre = tooth_index * pitch
    return (
        arc_points(tooth_root_radius, centre - pitch / 2.0, centre - tooth_base_half_angle_degrees, tooth_root_arc_steps)
        + [polar_point(tooth_tip_radius, centre - tooth_tip_half_angle_degrees)]
        + arc_points(tooth_tip_radius, centre - tooth_tip_half_angle_degrees, centre + tooth_tip_half_angle_degrees, tooth_tip_arc_steps)
        + [polar_point(tooth_root_radius, centre + tooth_base_half_angle_degrees)]
        + arc_points(tooth_root_radius, centre + tooth_base_half_angle_degrees, centre + pitch / 2.0, tooth_root_arc_steps)
    )


def gear_outline_points() -> list[App.Vector]:
    pts: list[App.Vector] = []
    for i in range(teeth_count):
        pts.extend(tooth_points(i))
    cleaned: list[App.Vector] = []
    for p in pts:
        if not cleaned or (p.sub(cleaned[-1])).Length > 1e-7:
            cleaned.append(p)
    if cleaned and (cleaned[0].sub(cleaned[-1])).Length < 1e-7:
        cleaned.pop()
    return cleaned


def make_gear_shape() -> Part.Shape:
    pts = gear_outline_points()
    wire = Part.makePolygon(pts + [pts[0]])
    face = Part.Face(wire)
    gear = face.extrude(App.Vector(0.0, 0.0, gear_thickness))
    gear.translate(App.Vector(0.0, 0.0, -gear_thickness / 2.0))
    if bore_diameter > 0:
        clearance = 0.02
        bore = Part.makeCylinder(
            bore_diameter / 2.0,
            gear_thickness + 2.0 * clearance,
            App.Vector(0.0, 0.0, -(gear_thickness + 2.0 * clearance) / 2.0),
            App.Vector(0.0, 0.0, 1.0),
            360.0,
        )
        gear = gear.cut(bore)
    if include_shaft and shaft_diameter > 0 and shaft_length > 0:
        shaft = Part.makeCylinder(
            shaft_diameter / 2.0,
            shaft_length,
            App.Vector(0.0, 0.0, shaft_z_offset - shaft_length / 2.0),
            App.Vector(0.0, 0.0, 1.0),
            360.0,
        )
        gear = gear.fuse(shaft)
    try:
        gear = gear.removeSplitter()
    except Exception:
        pass
    return gear


def add_properties(obj) -> None:
    props = {
        "teeth_count": ("App::PropertyInteger", teeth_count),
        "gear_thickness_mm": ("App::PropertyFloat", gear_thickness),
        "tooth_tip_radius_mm": ("App::PropertyFloat", tooth_tip_radius),
        "tooth_root_radius_mm": ("App::PropertyFloat", tooth_root_radius),
        "bore_diameter_mm": ("App::PropertyFloat", bore_diameter),
        "include_shaft": ("App::PropertyBool", include_shaft),
        "shaft_diameter_mm": ("App::PropertyFloat", shaft_diameter),
        "shaft_length_mm": ("App::PropertyFloat", shaft_length),
        "shaft_z_offset_mm": ("App::PropertyFloat", shaft_z_offset),
        "tooth_tip_half_angle_degrees": ("App::PropertyFloat", tooth_tip_half_angle_degrees),
        "tooth_base_half_angle_degrees": ("App::PropertyFloat", tooth_base_half_angle_degrees),
        "tooth_tip_arc_steps": ("App::PropertyInteger", tooth_tip_arc_steps),
        "tooth_root_arc_steps": ("App::PropertyInteger", tooth_root_arc_steps),
    }
    for name, (ptype, value) in props.items():
        try:
            obj.addProperty(ptype, name, "OpenSCAD Parameters", "Parameter mirrored from AR4_SG1_gear.scad")
        except Exception:
            pass
        setattr(obj, name, value)


def main() -> int:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    out_path = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(script_dir, "AR4_SG1_gear.FCStd")
    doc = App.newDocument("AR4_SG1_gear")
    obj = doc.addObject("Part::Feature", "AR4_SG1_gear_with_shaft")
    obj.Label = "AR4 SG1 gear with shaft (from SCAD parameters)"
    obj.Shape = make_gear_shape()
    add_properties(obj)
    try:
        obj.ViewObject.ShapeColor = (0.78, 0.78, 0.82, 0.0)
        obj.ViewObject.DisplayMode = "Flat Lines"
    except Exception:
        pass
    doc.recompute()
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    doc.saveAs(out_path)
    print("WROTE", out_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
