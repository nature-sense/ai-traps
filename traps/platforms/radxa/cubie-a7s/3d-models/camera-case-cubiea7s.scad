include <Round-Anything/polyround.scad>

base_side = 72;
base_thickness = 3;
side_thickness = 2;

side_height = 26.0;

lid_height = 10;

camera_base_side = 32;
camera_mount_side = 28;
camera_mount_post = 5;
camera_lens_hole = 16;

side_mount = 17;
side_mount_hole = 2.5;

base_width = 42;

side_outer = base_side + 2 * side_thickness + 8;
small_outer_width = base_width + 2 * side_thickness;

small_rim_outer_width = base_width + 2;
rim_outer = base_side + 2;
rim_thickness = 1;

usb_x = 17;
reset_1_x = 29.5;
reset_2_x = 37.75;

usb_width = 10;
usb_height = 4.5;
reset_width = 2;
reset_height = 2.5;

camera_mount_hole = 2.0;
camera_mount_offset = 15;

sbc_length = 50.8;
sbc_width = 50.8;
sbc_mount_length = 43.8;
sbc_mount_width = 43.8;
sbc_mount_hole = 2.5;
sbc_mount_post = 3;

sbc_offset = 21;

cable_brace_width = 26;
cable_brace_length = 3.5;
cable_brace_height = sbc_mount_post + 1.5;

screen_mount_width = 10;
screen_mount_length = 34;

screenmount_cutout_width = 18;
screenmount_cutout_height = 6;

screen_width = 30.5;
screen_length = 65.5;
screen_thickness = 2.0;

mount_height = 19.5;

trap_height = 128;
trap_width = 88;

battery_len = 85;
battery_width = 62;
battery_mount_thickness = 2;
battery_mount_height = 25;

corner_side = 12;

connector_height = 9;
connector_offset = side_height/2 - connector_height + usb_height;

cam_4k_camera_mount = 28;
cam_4k_camera_side = 32;

cam_4k_camera_hole = 16;
cam_4k_camera_mount_height = 5;
cam_4k_camera_mount_hole = 2.5;

cam_4k_bracket_mount = 28;
cam_4k_bracket_hole = 3;
cam_4k_bracket_thickness = 3;
cam_4k_bracket_length = cam_4k_camera_side + 5;
cam_4k_bracket_width = sbc_width + 12;
cam_4k_bracket_hole_len = cam_4k_bracket_width - 8;

cam_4k_side_len = trap_width - 8;
cam_4k_side_height = 35;
cam_4k_side_depth = 5;
cam_4k_side_thickness = 5;

$fn = 128;

base_polygon = [
    [-base_side/2, base_side/2, 5],
    [base_side/2, base_side/2, 5],
    [base_side/2, -base_side/2, 5],
    [-base_side/2, -base_side/2, 5],
];

small_base_polygon = [
    [-base_side/2, base_width/2, 3],
    [base_side/2, base_width/2, 3],
    [base_side/2, -base_width/2, 3],
    [-base_side/2, -base_width/2, 3],
];

small_outer_polygon = [
    [-side_outer/2, small_outer_width/2, 3],
    [side_outer/2, small_outer_width/2, 3],
    [side_outer/2, -small_outer_width/2, 3],
    [-side_outer/2, -small_outer_width/2, 3]
];

screen_polygon = [
    [-screen_length/2, screen_width/2, 2],
    [screen_length/2, screen_width/2, 2],
    [screen_length/2, -screen_width/2, 2],
    [-screen_length/2, -screen_width/2, 2]
];

small_rim_outer_polygon = [
    [-rim_outer/2, small_rim_outer_width/2, 2],
    [rim_outer/2, small_rim_outer_width/2, 2],
    [rim_outer/2, -small_rim_outer_width/2, 2],
    [-rim_outer/2, -small_rim_outer_width/2, 2]
];

battery_corner_polygon = [
    [0,0,8],
    [0,corner_side+2,0],
    [2,corner_side+2,0],
    [2,2,8],
    [2+corner_side,2,0],
    [2+corner_side, 0,0]
];

battery_base_polygon = [
    [-battery_len/2, battery_width/2, 8],
    [battery_len/2, battery_width/2, 8],
    [battery_len/2, -battery_width/2, 8],
    [-battery_len/2, -battery_width/2, 8],
];

battery_base_cover_polygon = [
    [-battery_len/2 + 2.5, battery_width/2 - 2.5, 8],
    [battery_len/2 - 2.5, battery_width/2 - 2.5, 8],
    [battery_len/2 - 2.5, -battery_width/2 + 2.5, 8],
    [-battery_len/2 + 2.5, -battery_width/2 + 2.5, 8],
];

cam_4k_mount_polygon = [
    [-cam_4k_bracket_length/2, cam_4k_bracket_width/2,3],
    [cam_4k_bracket_length/2, cam_4k_bracket_width/2,3],
    [cam_4k_bracket_length/2, -cam_4k_bracket_width/2,3],
    [-cam_4k_bracket_length/2, -cam_4k_bracket_width/2,3]
];

camera_mount_polygon = [
    [-cam_4k_side_len/2, 0, 0],
    [-cam_4k_side_len/2, cam_4k_side_height, 5],
    [-cam_4k_bracket_length/2, cam_4k_side_height, 0],
    [-cam_4k_bracket_length/2, cam_4k_side_height-cam_4k_bracket_thickness/2, 0],
    [cam_4k_bracket_length/2, cam_4k_side_height-cam_4k_bracket_thickness/2, 0],
    [cam_4k_bracket_length/2, cam_4k_side_height, 0],
    [cam_4k_side_len/2, cam_4k_side_height, 5],
    [cam_4k_side_len/2, 0, 0],
    [cam_4k_side_len/2-cam_4k_side_depth, 0, 0],
    [cam_4k_side_len/2-cam_4k_side_depth, cam_4k_side_height-cam_4k_side_depth, 3],
    [-cam_4k_side_len/2+cam_4k_side_depth, cam_4k_side_height-cam_4k_side_depth, 3],
    [-cam_4k_side_len/2+cam_4k_side_depth, 0, 0]
];

// ======================================================================
// Active render — uncomment the module you want to preview
// ======================================================================
// lower();
// base();
// mount_base();
// trap_camera_mount();
// lid();
// cable_clamp();
// small_rim();
// small_side();
// small_lid();
// small_base();
// camera_mount();
sbc_mount();
// screen_mount();
// test_mount();
// screen_cutout();
// trap_base();
// battery_corner();
// trap_battery_mount();
// battery_base_cover();
// camera_base_cover();
// nose();
// trap_bubble_mount();
// trap_bubble_cover();
// trap_front();
// cooling_slot(20, 3, 4);
// lid_illill(3);
//translate([0,0.25,37])
//    rotate([0,180,0])
//cam_4k_camera_mount();
//camera_mount_side();

// ======================================================================

module camera_mount_side() {
//cam_4k_camera_mount
    
    difference() {
        rotate([90,0,0]) {
            linear_extrude(cam_4k_side_thickness) {
                polygon(polyRound(camera_mount_polygon, 256));
            }
        }
        union() {
            
        //-cam_4k_bracket_thickness/2-1//
        translate([cam_4k_camera_mount/2, -cam_4k_side_thickness/2, cam_4k_side_height-cam_4k_bracket_thickness*3/2-2])
                linear_extrude(cam_4k_bracket_thickness*2)
                    circle(d=2.5, $fn=128);
        
        translate([-cam_4k_camera_mount/2, -cam_4k_side_thickness/2, cam_4k_side_height-cam_4k_bracket_thickness*3/2-2])
                linear_extrude(cam_4k_bracket_thickness*2)
                    circle(d=2.5, $fn=128);
        
        }
    }
}

// ======================================================================
// The base of the module including mounts for the camera and the cubie a7s
// ======================================================================

module lower() {
    union() {
        translate([14,6,0])
            rotate([0,0,90])
                sbc_mount();
    }
}

// ======================================================================
// helper function for mount posts
// ======================================================================
module post(dia, height) {
    difference() {
        klam_fillet_cylinder(
            cylinder_height=height,
            cylinder_radius=(dia+2.5)/2,
            fillet_radius_bottom=2,
            fillet_radius_top=0,
            nfaces=50);
        linear_extrude(height)
            circle(d=dia, $fn=128);
    }
}

// ======================================================================
// Luckfox SBC mount
// ======================================================================
module sbc_mount() {
    union() {
        linear_extrude(base_thickness)
            square([trap_width-8, sbc_width+12], true);

        translate([-14,0,0]) {
            translate([-sbc_mount_width/2,sbc_mount_length/2,base_thickness])
                post(sbc_mount_hole,sbc_mount_post);
            translate([sbc_mount_width/2,sbc_mount_length/2,base_thickness])
                post(sbc_mount_hole,sbc_mount_post);
            translate([sbc_mount_width/2,-sbc_mount_length/2,base_thickness])
                post(sbc_mount_hole,sbc_mount_post);
            translate([-sbc_mount_width/2,-sbc_mount_length/2,base_thickness])
                post(sbc_mount_hole,sbc_mount_post);
        }
        translate([0, (sbc_mount_width+12)/2 + 4,0])
                camera_mount_side();
        translate([0, -(sbc_mount_width+12)/2+1,0])
                camera_mount_side();
    }
}

module cable_clamp() {
    clip_width = 10;
    clip_length = 30;
    clip_thickness = 4;
    clip_slot = 18;

    clamp_polygon = [
        [-clip_width/2, clip_length/2, 3],
        [clip_width/2, clip_length/2, 3],
        [clip_width/2, -clip_length/2, 3],
        [-clip_width/2, -clip_length/2, 3],
    ];

    difference() {
        linear_extrude(clip_thickness)
            polygon(polyRound(clamp_polygon, 256));
        union() {
            translate([-2.7,sbc_mount_width/2,0])
                linear_extrude(clip_thickness)
                    circle(d=2.6, $fn=128);
            translate([-2.7,-sbc_mount_width/2,0])
                linear_extrude(clip_thickness)
                    circle(d=2.6, $fn=128);
            translate([-3.2,0,clip_thickness-1.2])
                linear_extrude(1.2)
                    square([4,clip_slot], true);
        }
    }
}

// ======================================================================
// Mount holes for the lower case and the lid
// ======================================================================
module mount_holes(height) {
    translate([ base_side/2+2.5, base_width/2 - 2,  0])
        linear_extrude(height)
            circle(d=3, $fn=128);

    translate([ -base_side/2-2.5, base_width/2 - 2,  0])
        linear_extrude(height)
            circle(d=3, $fn=128);

    translate([ base_side/2+2.5, -base_width/2 + 2,  0])
        linear_extrude(height)
            circle(d=3, $fn=128);

    translate([ -base_side/2-2.5, -base_width/2 + 2,  0])
        linear_extrude(height)
            circle(d=3, $fn=128);
}

// ======================================================================

module side_mount() {
    side_mount_side = 25;
    side_mount_thickness = 6;

    hole_spacing = side_mount_side-8;
    hole_dia = 2.25;

    mount_polygon = [
        [-side_mount_side/2, side_mount_side/2, 3],
        [side_mount_side/2, side_mount_side/2, 3],
        [side_mount_side/2, -side_mount_side/2, 3],
        [-side_mount_side/2, -side_mount_side/2, 3],
    ];

    translate([0,0,side_mount_side/2])
    rotate([90,90,0])

    difference() {
        linear_extrude(side_mount_thickness)
            polygon(polyRound(mount_polygon, 256));
        union() {
            translate([-hole_spacing/2, hole_spacing/2, 0])
                linear_extrude(side_mount_thickness)
                    circle(d=hole_dia, $fn=256);
            translate([hole_spacing/2, hole_spacing/2, 0])
                linear_extrude(side_mount_thickness)
                    circle(d=hole_dia, $fn=256);
            translate([hole_spacing/2, -hole_spacing/2, 0])
                linear_extrude(side_mount_thickness)
                    circle(d=hole_dia, $fn=256);
            translate([-hole_spacing/2, -hole_spacing/2, 0])
                linear_extrude(side_mount_thickness)
                    circle(d=hole_dia, $fn=256);
        }
    }
}

module mount_base() {
    thickness = 6;

    side_mount_side = 25;
    side_mount_thickness = 4;

    hole_spacing = side_mount_side-8;
    hole_dia = 2.25;

    difference() {
        linear_extrude(thickness) {
            polygon(polyRound(small_outer_polygon, 256));
        }
        union() {
            linear_extrude(thickness)
                circle(d=camera_lens_hole, $fn=128);
            mount_holes(thickness);
        }
    }
    translate([0, small_outer_width/2 + 12.5,  0])
        mount_base_bracket(thickness);
}

module mount_base_bracket(thickness) {
    bracket_side = 25;
    hole_spacing = bracket_side-8;
    hole_dia = 2.25;

    difference() {
        linear_extrude(thickness) {
            square([bracket_side,bracket_side], true);
        }
        union() {
            translate([-hole_spacing/2, hole_spacing/2, 0])
                linear_extrude(thickness)
                    circle(d=hole_dia, $fn=256);
            translate([hole_spacing/2, hole_spacing/2, 0])
                linear_extrude(thickness)
                    circle(d=hole_dia, $fn=256);
            translate([hole_spacing/2, -hole_spacing/2, 0])
                linear_extrude(thickness)
                    circle(d=hole_dia, $fn=256);
            translate([-hole_spacing/2, -hole_spacing/2, 0])
                linear_extrude(thickness)
                    circle(d=hole_dia, $fn=256);
        }
    }
}

module trap_base() {
    difference() {
        union() {
            difference() {
                linear_extrude(base_thickness)
                    square([trap_width, trap_height], true);
                union() {
                    translate([0,32,0])
                        linear_extrude(base_thickness)
                            square([trap_width-8, sbc_width+12], true);
                    translate([-1,-32,0])
                        linear_extrude(base_thickness)
                            square([battery_len, battery_width], true);
                }
            }
            translate([-6,46,0])
                rotate([0,0,270])
                    lower();
            translate([-1,-32,0])
                trap_battery_mount();
        }
        linear_extrude(1)
            mirror([1, 0, 0])
                text("Nature Sense", halign="center", valign = "center", size = 5);
    }
}

module trap_front() {
    difference() {
        linear_extrude(base_thickness)
            square([trap_width, trap_height], true);

        union() {
            translate([0,37,0])
                linear_extrude(base_thickness)
                    square([side_outer, small_outer_width], true);

            linear_extrude(1)
                mirror([1, 0, 0])
                    text("Nature Sense", halign="center", valign = "center", size = 5);
        }
    }

    translate([0,37,0])
        trap_bubble_mount();
}

module dummy_trap_base() {
    linear_extrude(base_thickness)
        square([trap_width, trap_height], true);
}

module trap_camera_mount() {
    mount_height = 10;
    outer = 34.75;
    inner = 34;
    spacer = 3;

    difference() {
        union() {
            linear_extrude(base_thickness)
                square([side_outer, small_outer_width], true);

            translate([0,0,spacer])
                linear_extrude(spacer)
                    polygon(polyRound(small_outer_polygon, 256));
        }
        union() {
            translate([ 0,0,  base_thickness])
                linear_extrude(spacer)
                    square([ outer + 16, small_outer_width], true);

            translate([ base_side/2+2.5, base_width/2 - 2,  base_thickness])
                linear_extrude(spacer)
                    circle(d=3, $fn=128);

            translate([ -base_side/2-2.5, base_width/2 - 2,  base_thickness])
                linear_extrude(spacer)
                    circle(d=3, $fn=128);

            translate([ base_side/2+2.5, -base_width/2 + 2,  base_thickness])
                linear_extrude(spacer)
                    circle(d=3, $fn=128);

            translate([ -base_side/2-2.5, -base_width/2 + 2,  base_thickness])
                linear_extrude(spacer)
                    circle(d=3, $fn=128);
        }
    }
}

module trap_bubble_mount() {
    mount_height = 10;
    outer = 34.75;
    inner = 34;
    spacer = 3;

    difference() {
        linear_extrude(base_thickness)
            square([side_outer, small_outer_width], true);
        union() {
            linear_extrude(base_thickness)
                circle(d=inner, $fn=256);
            translate([0,0,0.5])
                linear_extrude(base_thickness)
                    circle(d=outer, $fn=256);

            translate([ -outer/2, outer/2,  1])
                linear_extrude(2)
                    circle(d=2.5, $fn=128);

            translate([ outer/2, outer/2,  1])
                linear_extrude(2)
                    circle(d=2.5, $fn=128);

            translate([ outer/2, -outer/2,  1])
                linear_extrude(2)
                    circle(d=2.5, $fn=128);

            translate([ -outer/2, -outer/2,  1])
                linear_extrude(2)
                    circle(d=2.5, $fn=128);

            translate([ -base_side/2-2.5, -base_width/2 + 2,  base_thickness])
                linear_extrude(spacer)
                    circle(d=3, $fn=128);
        }
    }
}

module trap_bubble_cover() {
    cover_thickness = 2.5;
    outer = 34.75;
    inner = 34;

    difference() {
        linear_extrude(cover_thickness)
            square([side_outer * 2/3, small_outer_width], true);
        union() {
            linear_extrude(0.75)
                circle(d=outer, $fn=256);

            translate([0,0,0.75])
                linear_extrude(cover_thickness-0.75)
                    circle(d=inner, $fn=256);

            translate([ -outer/2, outer/2,  0])
                linear_extrude(cover_thickness)
                    circle(d=2.5, $fn=128);

            translate([ outer/2, outer/2,  0])
                linear_extrude(cover_thickness)
                    circle(d=2.5, $fn=128);

            translate([ outer/2, -outer/2,  0])
                linear_extrude(cover_thickness)
                    circle(d=2.5, $fn=128);

            translate([ -outer/2, -outer/2,  0])
                linear_extrude(cover_thickness)
                    circle(d=2.5, $fn=128);
        }
    }
}

module camera_base_cover() {
    thickness = 3;
    spacer = 4;
    inner = 34;
    outer = 34.75;

    difference() {
        union() {
            linear_extrude(thickness)
                polygon(polyRound(small_outer_polygon, 256));
            translate([0,0,thickness])
                linear_extrude(spacer)
                    square([ outer + 6, small_outer_width], true);
        }
        union() {
            linear_extrude(base_thickness + spacer)
                circle(d=inner, $fn=256);

            translate([base_side/2+2.5, base_width/2 - 2,  0])
                linear_extrude(thickness)
                    circle(d=3, $fn=128);

            translate([ -base_side/2-2.5, base_width/2 - 2,  0])
                linear_extrude(thickness)
                    circle(d=3, $fn=128);

            translate([ base_side/2+2.5, -base_width/2 + 2,  0])
                linear_extrude(thickness)
                    circle(d=3, $fn=128);

            translate([ -base_side/2-2.5, -base_width/2 + 2,  0])
                linear_extrude(thickness)
                    circle(d=3, $fn=128);

            translate([ base_side/3 + 5, base_width/3,  0])
                union() {
                    linear_extrude(thickness)
                        circle(d=2.5, $fn=128);
                    linear_extrude(2)
                        circle(d=5.0, $fn=128);
                }

            translate([ -base_side/3 - 5, base_width/3,  0])
                union() {
                    linear_extrude(thickness)
                        circle(d=2.5, $fn=128);
                    linear_extrude(2)
                        circle(d=5.0, $fn=128);
                }
            translate([ base_side/3 + 5, -base_width/3,  0])
                union() {
                    linear_extrude(thickness)
                        circle(d=2.5, $fn=128);
                    linear_extrude(2)
                        circle(d=5.0, $fn=128);
                }

            translate([ -base_side/3 - 5, -base_width/3,  0])
                union() {
                    linear_extrude(thickness)
                        circle(d=2.5, $fn=128);
                    linear_extrude(2)
                        circle(d=5.0, $fn=128);
                }
        }
    }
}

module trap_battery_mount() {
    round_base_thickness = 4;
    overall_thickness = round_base_thickness + base_thickness;

    linear_extrude(base_thickness)
        square([battery_len, battery_width], true);

    translate([0,0,base_thickness])
        battery_round_base(round_base_thickness);

    translate([-battery_len/2, -battery_width/2, overall_thickness])
        battery_corner();
    translate([-battery_len/2, battery_width/2, overall_thickness])
        rotate([0,0,270])
            battery_corner();
    translate([battery_len/2, battery_width/2, overall_thickness])
        rotate([0,0,180])
            battery_corner();
    translate([battery_len/2, -battery_width/2, overall_thickness])
        rotate([0,0,90])
            battery_corner();
}

module battery_round_base(thickness) {
    strap_width = 21;
    strap_thickness = 1.5;

    difference() {
        linear_extrude(thickness)
            polygon(polyRound(battery_base_polygon, 256));
        union() {
            translate([0,0,thickness-strap_thickness])
                linear_extrude(strap_thickness)
                    square([strap_width, battery_width], true);

            translate([-battery_len*3/8, battery_width/4, 0])
                linear_extrude(thickness)
                    circle(d=2.5, $fn=128);

            translate([battery_len*3/8, battery_width/4, 0])
                linear_extrude(thickness)
                    circle(d=2.5, $fn=128);

            translate([-battery_len*3/8, -battery_width/4, 0])
                linear_extrude(thickness)
                    circle(d=2.5, $fn=128);

            translate([battery_len*3/8, -battery_width/4, 0])
                linear_extrude(thickness)
                    circle(d=2.5, $fn=128);
        }
    }
}

module battery_base_cover() {
    strap_width = 21;

    thickness = 3;
    difference() {
        linear_extrude(thickness)
            polygon(polyRound(battery_base_cover_polygon, 256));
        union() {
            translate([-battery_len*3/8, battery_width/4, 0])
                countersunk_hole(3);

            translate([battery_len*3/8, battery_width/4, 0])
                countersunk_hole(3);

            translate([-battery_len*3/8, -battery_width/4, 0])
                countersunk_hole(3);

            translate([battery_len*3/8, -battery_width/4, 0])
                countersunk_hole(3);

            translate([0, battery_width/2-4, 0])
                linear_extrude(thickness)
                    square([strap_width,5], true);

            translate([0, -battery_width/2+4, 0])
                linear_extrude(thickness)
                    square([strap_width,5], true);
        }
    }
}

module battery_corner() {
    linear_extrude(battery_mount_height) {
        polygon(polyRound(battery_corner_polygon, 256));
    }
}

module countersunk_hole(thickness) {
    union() {
        linear_extrude(thickness)
            circle(d=2.5, $fn=128);
        translate([0,0,1])
            linear_extrude(thickness)
                circle(d=5, $fn=128);
    }
}

module tripod_holes(height) {
    translate([-side_mount/2, side_mount/2,0])
        linear_extrude(height)
            circle(d=2.5, $fn=128);
    translate([side_mount/2, side_mount/2,0])
        linear_extrude(height)
            circle(d=2.5, $fn=128);
    translate([side_mount/2, -side_mount/2,0])
        linear_extrude(height)
            circle(d=2.5, $fn=128);
    translate([-side_mount/2, -side_mount/2,0])
        linear_extrude(height)
            circle(d=2.5, $fn=128);
}

module cam_4k_camera_mount() {
    difference() {
        union() {
            linear_extrude(cam_4k_bracket_thickness)
                square([cam_4k_bracket_length,sbc_width+12.5],true);

            translate([-cam_4k_camera_mount/2, cam_4k_camera_mount/2, cam_4k_bracket_thickness])
                post(2.0,cam_4k_camera_mount_height);
            translate([cam_4k_camera_mount/2, cam_4k_camera_mount/2, cam_4k_bracket_thickness])
                post(2.0,cam_4k_camera_mount_height);
            translate([cam_4k_camera_mount/2, -cam_4k_camera_mount/2, cam_4k_bracket_thickness])
                post(2.0,cam_4k_camera_mount_height);
            translate([-cam_4k_camera_mount/2, -cam_4k_camera_mount/2, cam_4k_bracket_thickness])
                post(2.0,cam_4k_camera_mount_height);
                
  
        }
        union() {
        
        
            linear_extrude(cam_4k_bracket_thickness)
                circle(d=cam_4k_camera_hole, $fn=128);
                
             translate([0,cam_4k_bracket_width/2-cam_4k_side_thickness/2+0.5, cam_4k_bracket_thickness/2])
                linear_extrude(cam_4k_side_thickness) //cam_4k_bracket_thickness/2)
                    square([cam_4k_bracket_length, cam_4k_side_thickness], true);          
 
            translate([0,-cam_4k_bracket_width/2+cam_4k_side_thickness/2-0.5, cam_4k_bracket_thickness/2])
                linear_extrude(cam_4k_side_thickness) //cam_4k_bracket_thickness/2)
                    square([cam_4k_bracket_length, cam_4k_side_thickness], true);          
 
            translate([-cam_4k_camera_mount/2, cam_4k_bracket_width/2-2,0])
                linear_extrude(cam_4k_bracket_thickness)
                    circle(d=cam_4k_bracket_hole, $fn=128);
                    
            translate([cam_4k_camera_mount/2, cam_4k_bracket_width/2-2,0])
                linear_extrude(cam_4k_bracket_thickness)
                    circle(d=cam_4k_bracket_hole, $fn=128);
                    
            translate([cam_4k_camera_mount/2,-cam_4k_bracket_width/2+2,0])
                linear_extrude(cam_4k_bracket_thickness)
                    circle(d=cam_4k_bracket_hole, $fn=128);
                    
            translate([-cam_4k_camera_mount/2,-cam_4k_bracket_width/2+2,0])
                linear_extrude(cam_4k_bracket_thickness)
                    circle(d=cam_4k_bracket_hole, $fn=128);
        }
    }
}

module klam_fillet_cylinder(
    cylinder_height=2,
    cylinder_radius=1,
    fillet_radius_bottom=1,
    fillet_radius_top=0,
    nfaces=50
) {
    /* created by Kevin Lam on Dec 3, 2016 */
    union() {
        cylinder(cylinder_height, r=cylinder_radius, $fn=nfaces, false);

        if (fillet_radius_bottom > 0) {
            difference() {
                cylinder(fillet_radius_bottom, r=cylinder_radius+fillet_radius_bottom, $fn=nfaces, false);
                translate([0, 0, fillet_radius_bottom])
                rotate_extrude($fn=nfaces)
                translate([cylinder_radius+fillet_radius_bottom, 0, 0])
                circle(fillet_radius_bottom, $fn=nfaces);
            }
        }

        if (fillet_radius_top>0) {
            difference() {
                translate([0,0,cylinder_height-fillet_radius_top])
                cylinder(fillet_radius_top, r=cylinder_radius+fillet_radius_top, $fn=nfaces, false);

                translate([0, 0, cylinder_height-fillet_radius_top])
                rotate_extrude($fn=nfaces)
                translate([cylinder_radius+fillet_radius_top, 0, 0])
                circle(fillet_radius_top, $fn=nfaces);
            }
        }
    }
}
