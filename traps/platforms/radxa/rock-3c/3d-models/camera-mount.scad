include <Round-Anything/polyround.scad>

box_outer = 44;
side_thickness = 1.5;
box_inner = box_outer - 2 * side_thickness;
box_height = 10;

camera_mount_side = 28;
camera_mount_post = 5; //9;
camera_lens_hole = 16;
camera_mount_hole = 2.0;


box_outer_polygon = [
    [-box_outer/2, box_outer/2, 4],
    [box_outer/2, box_outer/2, 4],
    [box_outer/2, -box_outer/2, 4],
    [-box_outer/2, -box_outer/2, 4]
];

box_inner_polygon = [
    [-box_inner/2, box_inner/2, 4],
    [box_inner/2, box_inner/2, 4],
    [box_inner/2, -box_inner/2, 4],
    [-box_inner/2, -box_inner/2, 4]
];


lower();

module lower() {
    difference() {
        union() {
            difference() {
                linear_extrude(box_height)
                    polygon(polyRound(box_outer_polygon, 256));
                translate([0,0,side_thickness])
                    linear_extrude(box_height-side_thickness)
                        polygon(polyRound(box_inner_polygon, 256));
            }
            translate([-camera_mount_side/2,camera_mount_side/2,side_thickness])
                post(camera_mount_hole,camera_mount_post);
            translate([camera_mount_side/2,camera_mount_side/2,side_thickness])
                post(camera_mount_hole,camera_mount_post);
            translate([camera_mount_side/2,-camera_mount_side/2,side_thickness])
                post(camera_mount_hole,camera_mount_post);
            translate([-camera_mount_side/2,- camera_mount_side/2,side_thickness])
                post(camera_mount_hole,camera_mount_post);
            
        }
        linear_extrude(box_height) {
            circle(d=camera_lens_hole, $fn=128);
        }
    }
}

module post(dia, height) {

    linear_extrude(height) {
        difference() {
            circle(d=dia+2.5, $fn=128);
            circle(d=dia, $fn=128);
        }
    }
}

