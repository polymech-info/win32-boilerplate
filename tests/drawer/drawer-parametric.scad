// Parametric folded-sheet drawer organizer concept
// Units: millimetres
//
// This model uses one stock sheet for the folded outer tray and separate
// slotted divider plates for flexible pocket sizes. Change STOCK SHEET SIZE
// and GRID below, then open in OpenSCAD and press F5/F6.

/* [View] */
view_mode = "assembled"; // [assembled, flat]
show_labels = true;

/* [Stock sheet size] */
stock_sheet_x = 420;      // full stock sheet width before folding
stock_sheet_y = 300;      // full stock sheet depth before folding
sheet_thickness = 2.5;    // plastic sheet thickness

/* [Folded tray] */
wall_height = 45;         // folded-up outer side height
fold_radius_hint = 2;     // visual only; fold lines are shown in flat mode
corner_gap = 1.0;         // relief gap at folded corners

/* [Grid / pockets] */
cols = 3;                 // pockets across width, e.g. 2 or 3
rows = 2;                 // pockets front-to-back, e.g. 2

/* [Slotted divider plates] */
divider_height = 42;
divider_thickness = 2.5;
slot_clearance = 0.35;
slot_depth_ratio = 0.55;  // each crossing slot depth as fraction of divider height
divider_edge_margin = 8;

/* [Flat layout spacing] */
flat_spacing = 18;
cut_line_width = 0.8;

$fn = 32;
eps = 0.02;

floor_x = stock_sheet_x - 2 * wall_height;
floor_y = stock_sheet_y - 2 * wall_height;
slot_w = divider_thickness + slot_clearance;
slot_d = divider_height * slot_depth_ratio;

assert(floor_x > 20, "stock_sheet_x must be greater than 2 * wall_height + 20");
assert(floor_y > 20, "stock_sheet_y must be greater than 2 * wall_height + 20");
assert(cols >= 1, "cols must be >= 1");
assert(rows >= 1, "rows must be >= 1");

if (view_mode == "assembled") {
    assembled_drawer();
} else {
    flat_cut_layout();
}

module assembled_drawer() {
    color("#e9edf2") tray_body();

    // Front-back divider plates: divide columns.
    for (i = [1 : cols - 1]) {
        x = i * floor_x / cols;
        translate([wall_height + x - divider_thickness / 2, wall_height, sheet_thickness])
            color("#9ecae1") front_back_divider();
    }

    // Left-right divider plates: divide rows.
    for (j = [1 : rows - 1]) {
        y = j * floor_y / rows;
        translate([wall_height, wall_height + y - divider_thickness / 2, sheet_thickness])
            color("#fdae6b") left_right_divider();
    }

    if (show_labels) assembled_labels();
}

module tray_body() {
    // Floor.
    cube([stock_sheet_x, stock_sheet_y, sheet_thickness]);

    // Folded outer walls, shown upright along the four fold lines.
    translate([0, 0, sheet_thickness])
        cube([stock_sheet_x, sheet_thickness, wall_height]);
    translate([0, stock_sheet_y - sheet_thickness, sheet_thickness])
        cube([stock_sheet_x, sheet_thickness, wall_height]);
    translate([0, 0, sheet_thickness])
        cube([sheet_thickness, stock_sheet_y, wall_height]);
    translate([stock_sheet_x - sheet_thickness, 0, sheet_thickness])
        cube([sheet_thickness, stock_sheet_y, wall_height]);

    // Small corner relief markers.
    color("#d95f0e") {
        translate([wall_height - corner_gap / 2, wall_height - corner_gap / 2, sheet_thickness + 0.2])
            cube([corner_gap, corner_gap, 1]);
        translate([stock_sheet_x - wall_height - corner_gap / 2, wall_height - corner_gap / 2, sheet_thickness + 0.2])
            cube([corner_gap, corner_gap, 1]);
        translate([wall_height - corner_gap / 2, stock_sheet_y - wall_height - corner_gap / 2, sheet_thickness + 0.2])
            cube([corner_gap, corner_gap, 1]);
        translate([stock_sheet_x - wall_height - corner_gap / 2, stock_sheet_y - wall_height - corner_gap / 2, sheet_thickness + 0.2])
            cube([corner_gap, corner_gap, 1]);
    }
}

module front_back_divider() {
    // Plate running in Y direction, with top-down slots at row crossings.
    difference() {
        cube([divider_thickness, floor_y, divider_height]);
        for (j = [1 : rows - 1]) {
            y = j * floor_y / rows;
            translate([-eps, y - slot_w / 2, divider_height - slot_d])
                cube([divider_thickness + 2 * eps, slot_w, slot_d + eps]);
        }
    }
}

module left_right_divider() {
    // Plate running in X direction, with bottom-up slots at column crossings.
    difference() {
        cube([floor_x, divider_thickness, divider_height]);
        for (i = [1 : cols - 1]) {
            x = i * floor_x / cols;
            translate([x - slot_w / 2, -eps, -eps])
                cube([slot_w, divider_thickness + 2 * eps, slot_d + eps]);
        }
    }
}

module flat_cut_layout() {
    // One-piece stock sheet flat pattern.
    color("#f7f7f7") cube([stock_sheet_x, stock_sheet_y, sheet_thickness]);

    // Fold lines.
    color("#2b8cbe") {
        line2d([wall_height, 0], [wall_height, stock_sheet_y], cut_line_width);
        line2d([stock_sheet_x - wall_height, 0], [stock_sheet_x - wall_height, stock_sheet_y], cut_line_width);
        line2d([0, wall_height], [stock_sheet_x, wall_height], cut_line_width);
        line2d([0, stock_sheet_y - wall_height], [stock_sheet_x, stock_sheet_y - wall_height], cut_line_width);
    }

    // Corner relief cuts.
    color("#d95f0e") {
        translate([wall_height - corner_gap / 2, wall_height - corner_gap / 2, sheet_thickness])
            cube([corner_gap, corner_gap, 0.8]);
        translate([stock_sheet_x - wall_height - corner_gap / 2, wall_height - corner_gap / 2, sheet_thickness])
            cube([corner_gap, corner_gap, 0.8]);
        translate([wall_height - corner_gap / 2, stock_sheet_y - wall_height - corner_gap / 2, sheet_thickness])
            cube([corner_gap, corner_gap, 0.8]);
        translate([stock_sheet_x - wall_height - corner_gap / 2, stock_sheet_y - wall_height - corner_gap / 2, sheet_thickness])
            cube([corner_gap, corner_gap, 0.8]);
    }

    // Divider plates laid out beside the stock sheet for CNC/laser reference.
    translate([0, -(divider_height + flat_spacing), 0]) flat_front_back_dividers();
    translate([0, -(2 * divider_height + 2 * flat_spacing), 0]) flat_left_right_dividers();

    if (show_labels) flat_labels();
}

module flat_front_back_dividers() {
    count = max(cols - 1, 0);
    for (i = [0 : count - 1]) {
        translate([i * (floor_y + flat_spacing), 0, 0])
            rotate([0, 0, 0]) flat_front_back_plate();
    }
}

module flat_left_right_dividers() {
    count = max(rows - 1, 0);
    for (j = [0 : count - 1]) {
        translate([j * (floor_x + flat_spacing), 0, 0])
            flat_left_right_plate();
    }
}

module flat_front_back_plate() {
    // 2D-looking plate lying flat: length is floor_y, height is divider_height.
    difference() {
        color("#9ecae1") cube([floor_y, divider_height, sheet_thickness]);
        for (j = [1 : rows - 1]) {
            x = j * floor_y / rows;
            translate([x - slot_w / 2, divider_height - slot_d, -eps])
                cube([slot_w, slot_d + eps, sheet_thickness + 2 * eps]);
        }
    }
}

module flat_left_right_plate() {
    // 2D-looking plate lying flat: length is floor_x, height is divider_height.
    difference() {
        color("#fdae6b") cube([floor_x, divider_height, sheet_thickness]);
        for (i = [1 : cols - 1]) {
            x = i * floor_x / cols;
            translate([x - slot_w / 2, -eps, -eps])
                cube([slot_w, slot_d + eps, sheet_thickness + 2 * eps]);
        }
    }
}

module line2d(a, b, w) {
    hull() {
        translate([a[0], a[1], sheet_thickness]) cylinder(h = 0.6, d = w);
        translate([b[0], b[1], sheet_thickness]) cylinder(h = 0.6, d = w);
    }
}

module assembled_labels() {
    echo(str("Stock sheet: ", stock_sheet_x, " x ", stock_sheet_y, " x ", sheet_thickness, " mm"));
    echo(str("Folded floor inside: ", floor_x, " x ", floor_y, " mm"));
    echo(str("Grid: ", cols, " x ", rows, " pockets"));
    echo(str("Pocket approx: ", floor_x / cols, " x ", floor_y / rows, " mm before divider thickness"));
}

module flat_labels() {
    echo("Blue lines = fold lines");
    echo("Orange marks = corner relief cuts");
    echo(str("Divider plates: ", max(cols - 1, 0), " front-back + ", max(rows - 1, 0), " left-right"));
}
