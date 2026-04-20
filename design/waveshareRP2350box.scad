$fn = 50;
box();
//translate([0,0,10])lid();
module box() {
  difference() {
    translate([-1, -1, -2])cube([17.5 + 2, 33.5 + 2, 10.7]);
    pcb(10);
  }
}
module lid() {
  difference() {
    union() {
      translate([-1, -1, 1])
      { cube([17.5 + 2, 33.5 + 2, 1]);
        translate([1, 1, -0.7])hull() {
          translate([2, 2, 0])cylinder(d = 4, h = 1);
          translate([17.5 - 2, 2, 0])cylinder(d = 4, h = 1);
          translate([17.5 - 2, 33.5 - 2, 0])cylinder(d = 4, h = 1);
          translate([2, 33.5 - 2, 0])cylinder(d = 4, h = 1);
        }
      }
    }
    translate([2, -1.01, 0])cube([13.5, 10, 1]);
    translate([4.75, 10, 0])cube([8, 14, 10]);
  }
}
module pcb(he) {
  hull() {
    translate([2, 2, 0])cylinder(d = 4, h = he);
    translate([17.5 - 2, 2, 0])cylinder(d = 4, h = he);
    translate([17.5 - 2, 33.5 - 2, 0])cylinder(d = 4, h = he);
    translate([2, 33.5 - 2, 0])cylinder(d = 4, h = he);
  }
  translate([2, -1.01, 1.8])cube([13.5, 10, 7]);
  translate([8.5 / 2, 33.5 - 5.5, 1.8 + 1.5])rotate([-90, 0, 0])hull(){
    translate([1.5, 0, 0])cylinder(d = 3, h = 7);
    translate([9 - 1.5, 0, 0])cylinder(d = 3, h = 7);
  }
}
