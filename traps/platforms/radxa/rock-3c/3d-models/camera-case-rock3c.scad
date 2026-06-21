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


camera_mount_hole = 2.0;
camera_mount_offset = 15;

sbc_length = 85.0;
sbc_width = 56.0;

sbc_plate_length = 90;
sbc_plate_width = 85;

sbc_end_mount_offset = -(sbc_length/2 - 3.6);
sbc_centre_mount_offset = 58.0 + 3.5 -sbc_length/2;

sbc_mount_height = 15;

trap_height = 128;
trap_width = 90;

battery_len = 85;
battery_width = 62;
battery_mount_thickness = 2;
battery_mount_height = 25;

corner_side = 12;


lens_outer = 34.75;
lens_inner = 34;
lens_inner_length = 0.75;

camera_mount_length = 60;
camera_mount_width = 38;
camera_mount_post_w = 7;
camera_mount_post_x = camera_mount_length - camera_mount_post_w;
camera_mount_post_y = camera_mount_width - camera_mount_post_w;
camera_mount_post_z = 2;

$fn = 128;


sbc_mount_polygon = [
    [-sbc_width/2,0, 0],
    [-sbc_width/2, sbc_mount_height, 0],
    [-sbc_width/2+ 7, sbc_mount_height, 0],
    [-sbc_width/2+ 7, base_thickness, 3],
    [sbc_width/2 - 7, base_thickness, 3],
    [sbc_width/2 - 7, sbc_mount_height, 0],
    [sbc_width/2, sbc_mount_height, 0],
    [sbc_width/2,0, 0],

];

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
//sbc_mount();
sbc_plate();
//camera_plate();
//pi_camera_1_3();
//pi_camera_1_3_lug();
//trap_bubble_mount();
//sbc_mount_support();
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


// =========================================================
// Camera Plate
// =========================================================
module camera_plate() {

    difference() {
        union() {
            linear_extrude(2) 
                square([camera_mount_length, camera_mount_width], true);
            linear_extrude(2+camera_mount_post_z) 
                square([camera_mount_length-2*camera_mount_post_w, camera_mount_width], true);
        }
        union() {
            translate([camera_mount_post_x/2, camera_mount_post_y/2, 0])
                linear_extrude(10)
                    circle(d=2.5, $fn=128);
            translate([camera_mount_post_x/2, -camera_mount_post_y/2, 0])
                linear_extrude(10)
                    circle(d=2.5, $fn=128);        
            translate([-camera_mount_post_x/2, camera_mount_post_y/2, 0])
                linear_extrude(10)
                    circle(d=2.5, $fn=128);        
            translate([-camera_mount_post_x/2, -camera_mount_post_y/2, 0])
                linear_extrude(10)
                    circle(d=2.5, $fn=128);
        }
    }
}


module pi_camera_1_3() {

    difference() {
        camera_plate();
        translate([0,1.5,0])
            linear_extrude(10)
                square([25,24], true);
    }
    translate([10.5,-1.5,0])
        pi_camera_1_3_lug();
    translate([-10.5,-1.5,0])
        rotate([0,0,180])
            pi_camera_1_3_lug();
            
    translate([10.5,11,0])
        pi_camera_1_3_lug();
    translate([-10.5,11,0])
        rotate([0,0,180])
            pi_camera_1_3_lug();
}    
    
module pi_camera_1_3_lug() {
    difference() {
        union() {
            linear_extrude(2+camera_mount_post_z)
                circle(d=4, $fn=128);
            translate([2.0/2,0,0])
                linear_extrude(2+camera_mount_post_z)
                    square([3,4], true);
        }
        linear_extrude(2+camera_mount_post_z)
            circle(d=2.0, $fn=128);
    }
}
    


// =========================================================
// Board mount pillars
// =========================================================

module sbc_mount_support() {
    
    difference() {
        translate([-3.5,0,0]) {
            rotate([90,0,90]) {
                linear_extrude(7) {
                    polygon(polyRound(sbc_mount_polygon, 256));
                }
            }
        }
        union() {
            translate([0, 49/2, sbc_mount_height/2])
                linear_extrude(sbc_mount_height/2)
                    circle(d=2.5, $fn=12);
           translate([0, -49/2, sbc_mount_height/2])
                linear_extrude(sbc_mount_height/2)
                    circle(d=2.5, $fn=12);        
        
        }
    }
}


// ======================================================================
// SBC base
// ======================================================================
module sbc_mount() {
    difference() {
        union() {
            linear_extrude(base_thickness)
                square([sbc_length, sbc_width], true);
            translate([sbc_end_mount_offset,0,0])
                sbc_mount_support();
            translate([sbc_centre_mount_offset,0,0])
                sbc_mount_support();
                
            translate([-camera_mount_post_x/2,0,base_thickness]) {
                linear_extrude(camera_mount_post_z)
                    square([camera_mount_post_w, camera_mount_width], true);
            }
            translate([camera_mount_post_x/2,0,base_thickness]) {
                linear_extrude(camera_mount_post_z)
                    square([camera_mount_post_w, camera_mount_width], true);
            }
        }
        union() {
            translate([camera_mount_post_x/2, camera_mount_post_y/2, 1])
                linear_extrude(10)
                    circle(d=2.5, $fn=128);
            translate([camera_mount_post_x/2, -camera_mount_post_y/2, 1])
                linear_extrude(10)
                    circle(d=2.5, $fn=128);        
            translate([-camera_mount_post_x/2, camera_mount_post_y/2, 1])
                linear_extrude(10)
                    circle(d=2.5, $fn=128);        
            translate([-camera_mount_post_x/2, -camera_mount_post_y/2, 1])
                linear_extrude(10)
                    circle(d=2.5, $fn=128);
        }
    }
}


// ======================================================================
// SBC plate - to be added to the trap base
// ======================================================================
module sbc_plate() {

    difference() {
        union() {
            difference() {
                linear_extrude(base_thickness) 
                    square([sbc_plate_length, sbc_plate_width], true);
                translate([0,12,0])
                    linear_extrude(base_thickness)
                        square([sbc_length, sbc_width], true);
            }
            translate([0,12,0])
                sbc_mount();
        }
        union() {
            translate([0,12,0])
                linear_extrude(base_thickness)
                    circle(d=lens_inner, $fn=128);
            translate([0,12,lens_inner_length])
               linear_extrude(base_thickness)
                    circle(d=lens_outer, $fn=128);
        }
    }
}

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
