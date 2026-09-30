// AR4_SG1_gear.scad
// Parametric OpenSCAD recreation of AR4_SG1_gear.STEP
// Units: mm
// Source STEP observations:
//   thickness = 0.9374 - (-5.0626) = 6.0
//   outer radius ~= 10.541
//   root/minor radius ~= 8.4431757
//   bore radius = 1.5
//   tooth centres repeat every 20 deg => 18 teeth
//
// This is a clean parametric approximation, not a literal BREP conversion.

$fn = 96;

// ---- Main gear parameters ----
// Number of teeth around the gear circumference.
teeth_count = 18;

// Axial thickness of the gear body.
gear_thickness = 6.0;

// Radius to the outside of each tooth tip.
tooth_tip_radius = 10.541;

// Radius to the bottom of each tooth valley.
tooth_root_radius = 8.4431757035;

// Diameter of the central shaft/bore hole.
bore_diameter = 3.0;

// ---- Counter gear parameters ----
// Counter gear disabled/hidden by default.
show_counter_gear = false;

counter_teeth_count       = teeth_count;
counter_tooth_tip_radius  = tooth_tip_radius;
counter_tooth_root_radius = tooth_root_radius;
counter_bore_diameter     = bore_diameter;
counter_gear_thickness    = gear_thickness;

// Distance between the two gear axes when the counter gear is shown.
counter_center_distance = tooth_tip_radius + counter_tooth_root_radius;

// Angular offset that places the counter gear teeth into the main gear valleys.
counter_mesh_phase_degrees = 360 / (2 * counter_teeth_count);

// ---- Shaft parameters ----
// Shaft is coaxial with the gear bore and the OpenSCAD Z axis.
// Set shaft_diameter <= 0 or shaft_length <= 0 to suppress the shaft in the
// ar4_sg1_gear_with_shaft() assembly.
shaft_diameter = bore_diameter;
shaft_length   = 18.0;
shaft_z_offset = 0.0;   // moves the shaft along Z relative to the gear centre
shaft_facets   = 96;

// ---- Tooth angular controls ----
// Defaults match the STEP tooth-tip angle closely.
// Both values are measured in degrees either side of each tooth centreline.
tooth_tip_half_angle_degrees  = 3.82;
tooth_base_half_angle_degrees = 8.25;

// ---- Faceting controls for the 2D outline ----
tooth_tip_arc_steps  = 4;
tooth_root_arc_steps = 2;
bore_facets          = 96;

// ---- Helpers ----
function polar_point(radius, angle_degrees) =
    [radius * cos(angle_degrees), radius * sin(angle_degrees)];

function arc_points(radius, start_angle_degrees, end_angle_degrees, segment_count) =
    [for (segment_index = [0:segment_count])
        polar_point(
            radius,
            start_angle_degrees + (end_angle_degrees - start_angle_degrees) * segment_index / segment_count
        )
    ];

function tooth_points(tooth_index,
                      gear_teeth_count=teeth_count,
                      tip_radius=tooth_tip_radius,
                      root_radius=tooth_root_radius,
                      tip_half_angle_degrees=tooth_tip_half_angle_degrees,
                      base_half_angle_degrees=tooth_base_half_angle_degrees,
                      tip_arc_segment_count=tooth_tip_arc_steps,
                      root_arc_segment_count=tooth_root_arc_steps) =
    let(
        tooth_pitch_degrees = 360 / gear_teeth_count,
        tooth_center_degrees = tooth_index * tooth_pitch_degrees
    )
    concat(
        arc_points(root_radius,
                   tooth_center_degrees - tooth_pitch_degrees/2,
                   tooth_center_degrees - base_half_angle_degrees,
                   root_arc_segment_count),       // root valley approaching tooth
        [polar_point(tip_radius,
                     tooth_center_degrees - tip_half_angle_degrees)],
        arc_points(tip_radius,
                   tooth_center_degrees - tip_half_angle_degrees,
                   tooth_center_degrees + tip_half_angle_degrees,
                   tip_arc_segment_count),         // rounded/flat-ish tooth tip
        [polar_point(root_radius,
                     tooth_center_degrees + base_half_angle_degrees)],
        arc_points(root_radius,
                   tooth_center_degrees + base_half_angle_degrees,
                   tooth_center_degrees + tooth_pitch_degrees/2,
                   root_arc_segment_count)         // root valley leaving tooth
    );

function gear_points(gear_teeth_count=teeth_count,
                     tip_radius=tooth_tip_radius,
                     root_radius=tooth_root_radius,
                     tip_half_angle_degrees=tooth_tip_half_angle_degrees,
                     base_half_angle_degrees=tooth_base_half_angle_degrees,
                     tip_arc_segment_count=tooth_tip_arc_steps,
                     root_arc_segment_count=tooth_root_arc_steps) =
    [for (tooth_index = [0:gear_teeth_count-1])
        each tooth_points(tooth_index,
                          gear_teeth_count,
                          tip_radius,
                          root_radius,
                          tip_half_angle_degrees,
                          base_half_angle_degrees,
                          tip_arc_segment_count,
                          root_arc_segment_count)
    ];

// ---- 2D profile ----
module ar4_sg1_gear_2d(gear_teeth_count=teeth_count,
                       tip_radius=tooth_tip_radius,
                       root_radius=tooth_root_radius,
                       tip_half_angle_degrees=tooth_tip_half_angle_degrees,
                       base_half_angle_degrees=tooth_base_half_angle_degrees,
                       tip_arc_segment_count=tooth_tip_arc_steps,
                       root_arc_segment_count=tooth_root_arc_steps) {
    polygon(points = gear_points(gear_teeth_count,
                                 tip_radius,
                                 root_radius,
                                 tip_half_angle_degrees,
                                 base_half_angle_degrees,
                                 tip_arc_segment_count,
                                 root_arc_segment_count));
}

// ---- Parametric shaft, axis along OpenSCAD Z ----
module parametric_shaft(shaft_diameter_value=shaft_diameter,
                        shaft_length_value=shaft_length,
                        z_offset=shaft_z_offset,
                        center_on_origin=true,
                        facet_count=shaft_facets) {
    if (shaft_diameter_value > 0 && shaft_length_value > 0) {
        translate([0, 0, z_offset])
            cylinder(h=shaft_length_value,
                     d=shaft_diameter_value,
                     center=center_on_origin,
                     $fn=facet_count);
    }
}

// ---- 3D gear, axis along OpenSCAD Z ----
module ar4_sg1_gear(gear_teeth_count=teeth_count,
                    gear_height=gear_thickness,
                    tip_radius=tooth_tip_radius,
                    root_radius=tooth_root_radius,
                    bore_diameter_value=bore_diameter,
                    tip_half_angle_degrees=tooth_tip_half_angle_degrees,
                    base_half_angle_degrees=tooth_base_half_angle_degrees,
                    tip_arc_segment_count=tooth_tip_arc_steps,
                    root_arc_segment_count=tooth_root_arc_steps,
                    center_on_origin=true) {
    cut_clearance = 0.02;
    difference() {
        linear_extrude(height=gear_height, center=center_on_origin, convexity=10)
            ar4_sg1_gear_2d(gear_teeth_count=gear_teeth_count,
                             tip_radius=tip_radius,
                             root_radius=root_radius,
                             tip_half_angle_degrees=tip_half_angle_degrees,
                             base_half_angle_degrees=base_half_angle_degrees,
                             tip_arc_segment_count=tip_arc_segment_count,
                             root_arc_segment_count=root_arc_segment_count);

        if (bore_diameter_value > 0)
            translate([0, 0, center_on_origin ? 0 : gear_height/2])
                cylinder(h=gear_height + 2*cut_clearance,
                         d=bore_diameter_value,
                         center=true,
                         $fn=bore_facets);
    }
}

// ---- Gear plus coaxial parametric shaft ----
module ar4_sg1_gear_with_shaft(gear_teeth_count=teeth_count,
                               gear_height=gear_thickness,
                               tip_radius=tooth_tip_radius,
                               root_radius=tooth_root_radius,
                               bore_diameter_value=bore_diameter,
                               tip_half_angle_degrees=tooth_tip_half_angle_degrees,
                               base_half_angle_degrees=tooth_base_half_angle_degrees,
                               tip_arc_segment_count=tooth_tip_arc_steps,
                               root_arc_segment_count=tooth_root_arc_steps,
                               shaft_diameter_value=shaft_diameter,
                               shaft_length_value=shaft_length,
                               shaft_z_offset_value=shaft_z_offset,
                               center_on_origin=true) {
    union() {
        ar4_sg1_gear(gear_teeth_count=gear_teeth_count,
                     gear_height=gear_height,
                     tip_radius=tip_radius,
                     root_radius=root_radius,
                     bore_diameter_value=bore_diameter_value,
                     tip_half_angle_degrees=tip_half_angle_degrees,
                     base_half_angle_degrees=base_half_angle_degrees,
                     tip_arc_segment_count=tip_arc_segment_count,
                     root_arc_segment_count=root_arc_segment_count,
                     center_on_origin=center_on_origin);
        parametric_shaft(shaft_diameter_value=shaft_diameter_value,
                         shaft_length_value=shaft_length_value,
                         z_offset=shaft_z_offset_value,
                         center_on_origin=center_on_origin,
                         facet_count=shaft_facets);
    }
}

// ---- Main gear plus optional meshing counter gear ----
module ar4_sg1_gear_pair(main_teeth_count=teeth_count,
                         main_gear_height=gear_thickness,
                         main_tip_radius=tooth_tip_radius,
                         main_root_radius=tooth_root_radius,
                         main_bore_diameter=bore_diameter,
                         counter_teeth_count_value=counter_teeth_count,
                         counter_tip_radius=counter_tooth_tip_radius,
                         counter_root_radius=counter_tooth_root_radius,
                         counter_bore_diameter_value=counter_bore_diameter,
                         counter_gear_height=counter_gear_thickness,
                         center_distance=counter_center_distance,
                         mesh_phase_degrees=counter_mesh_phase_degrees,
                         center_on_origin=true) {
    union() {
        ar4_sg1_gear(gear_teeth_count=main_teeth_count,
                     gear_height=main_gear_height,
                     tip_radius=main_tip_radius,
                     root_radius=main_root_radius,
                     bore_diameter_value=main_bore_diameter,
                     tip_half_angle_degrees=tooth_tip_half_angle_degrees,
                     base_half_angle_degrees=tooth_base_half_angle_degrees,
                     tip_arc_segment_count=tooth_tip_arc_steps,
                     root_arc_segment_count=tooth_root_arc_steps,
                     center_on_origin=center_on_origin);

        if (show_counter_gear)
            translate([center_distance, 0, 0])
                rotate([0, 0, mesh_phase_degrees])
                    ar4_sg1_gear(gear_teeth_count=counter_teeth_count_value,
                                 gear_height=counter_gear_height,
                                 tip_radius=counter_tip_radius,
                                 root_radius=counter_root_radius,
                                 bore_diameter_value=counter_bore_diameter_value,
                                 tip_half_angle_degrees=tooth_tip_half_angle_degrees,
                                 base_half_angle_degrees=tooth_base_half_angle_degrees,
                                 tip_arc_segment_count=tooth_tip_arc_steps,
                                 root_arc_segment_count=tooth_root_arc_steps,
                                 center_on_origin=center_on_origin);
    }
}

module ar4_sg1_gear_pair_with_shafts(main_teeth_count=teeth_count,
                                     main_gear_height=gear_thickness,
                                     main_tip_radius=tooth_tip_radius,
                                     main_root_radius=tooth_root_radius,
                                     main_bore_diameter=bore_diameter,
                                     counter_teeth_count_value=counter_teeth_count,
                                     counter_tip_radius=counter_tooth_tip_radius,
                                     counter_root_radius=counter_tooth_root_radius,
                                     counter_bore_diameter_value=counter_bore_diameter,
                                     counter_gear_height=counter_gear_thickness,
                                     center_distance=counter_center_distance,
                                     mesh_phase_degrees=counter_mesh_phase_degrees,
                                     shaft_diameter_value=shaft_diameter,
                                     shaft_length_value=shaft_length,
                                     shaft_z_offset_value=shaft_z_offset,
                                     center_on_origin=true) {
    union() {
        ar4_sg1_gear_with_shaft(gear_teeth_count=main_teeth_count,
                                gear_height=main_gear_height,
                                tip_radius=main_tip_radius,
                                root_radius=main_root_radius,
                                bore_diameter_value=main_bore_diameter,
                                shaft_diameter_value=shaft_diameter_value,
                                shaft_length_value=shaft_length_value,
                                shaft_z_offset_value=shaft_z_offset_value,
                                center_on_origin=center_on_origin);

        if (show_counter_gear)
            translate([center_distance, 0, 0])
                rotate([0, 0, mesh_phase_degrees])
                    ar4_sg1_gear_with_shaft(gear_teeth_count=counter_teeth_count_value,
                                            gear_height=counter_gear_height,
                                            tip_radius=counter_tip_radius,
                                            root_radius=counter_root_radius,
                                            bore_diameter_value=counter_bore_diameter_value,
                                            shaft_diameter_value=shaft_diameter_value,
                                            shaft_length_value=shaft_length_value,
                                            shaft_z_offset_value=shaft_z_offset_value,
                                            center_on_origin=center_on_origin);
    }
}

// STEP file used X as the gear axis, with faces at X=-5.0626 and X=0.9374.
// Use these wrappers if you want the same approximate orientation/position.
module ar4_sg1_gear_step_orientation() {
    translate([-2.0626, 0, 0])
        rotate([0, 90, 0])
            ar4_sg1_gear(center_on_origin=true);
}

module ar4_sg1_gear_with_shaft_step_orientation() {
    translate([-2.0626, 0, 0])
        rotate([0, 90, 0])
            ar4_sg1_gear_with_shaft(center_on_origin=true);
}

module ar4_sg1_gear_pair_step_orientation() {
    translate([-2.0626, 0, 0])
        rotate([0, 90, 0])
            ar4_sg1_gear_pair(center_on_origin=true);
}

module ar4_sg1_gear_pair_with_shafts_step_orientation() {
    translate([-2.0626, 0, 0])
        rotate([0, 90, 0])
            ar4_sg1_gear_pair_with_shafts(center_on_origin=true);
}

// ---- Render default ----
ar4_sg1_gear_with_shaft();

// Uncomment for alternate render options:
// ar4_sg1_gear();
// ar4_sg1_gear_pair();
// ar4_sg1_gear_pair_with_shafts();

// Uncomment for the original STEP-like X-axis placement instead:
// ar4_sg1_gear_step_orientation();
// ar4_sg1_gear_with_shaft_step_orientation();
// ar4_sg1_gear_pair_step_orientation();
// ar4_sg1_gear_pair_with_shafts_step_orientation();
