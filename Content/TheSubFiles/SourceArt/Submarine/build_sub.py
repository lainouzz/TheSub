"""Builds a low-poly co-op submarine for UE5 (TheSub).

Units: meters in Blender (1 m -> 100 cm in UE). Forward = +X, Up = +Z.
Origin = center of the hull (matches ASubmarine's HullCollision box center).
"""
import math
import os
import sys

import bpy  # must come first: registers bmesh/mathutils
import bmesh
from mathutils import Euler, Matrix, Vector

OUT = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else "/tmp/sub_out"
os.makedirs(OUT, exist_ok=True)

# ---------------------------------------------------------------- scene reset
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.unit_settings.system = "METRIC"
scene.unit_settings.scale_length = 1.0

# ---------------------------------------------------------------- materials
def make_mat(name, color, metallic=0.0, rough=0.6, emission=None, strength=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = rough
    if emission:
        bsdf.inputs["Emission Color"].default_value = (*emission, 1.0)
        bsdf.inputs["Emission Strength"].default_value = strength
    m.diffuse_color = (*color, 1.0)
    return m

MAT = {
    "Hull": make_mat("M_Sub_Hull", (0.11, 0.17, 0.19), 0.4, 0.55),
    "HullDark": make_mat("M_Sub_HullDark", (0.04, 0.05, 0.06), 0.4, 0.5),
    "Brass": make_mat("M_Sub_Brass", (0.55, 0.36, 0.10), 1.0, 0.35),
    "Wall": make_mat("M_Sub_Wall", (0.26, 0.31, 0.27), 0.0, 0.7),
    "Floor": make_mat("M_Sub_Floor", (0.08, 0.08, 0.09), 0.6, 0.6),
    "Metal": make_mat("M_Sub_Metal", (0.24, 0.25, 0.26), 0.8, 0.45),
    "Console": make_mat("M_Sub_Console", (0.10, 0.11, 0.12), 0.3, 0.5),
    "ScreenGreen": make_mat("M_Sub_ScreenGreen", (0.02, 0.15, 0.06), 0.0, 0.3, (0.1, 1.0, 0.4), 6.0),
    "ScreenAmber": make_mat("M_Sub_ScreenAmber", (0.15, 0.08, 0.01), 0.0, 0.3, (1.0, 0.55, 0.1), 6.0),
    "Warning": make_mat("M_Sub_Warning", (0.85, 0.60, 0.04), 0.0, 0.5),
    "Light": make_mat("M_Sub_Light", (0.9, 0.9, 0.8), 0.0, 0.3, (1.0, 0.92, 0.75), 12.0),
    "Red": make_mat("M_Sub_Red", (0.6, 0.03, 0.03), 0.0, 0.4, (1.0, 0.05, 0.02), 4.0),
}

# ---------------------------------------------------------------- mesh builder
class Builder:
    """Accumulates geometry into one bmesh with per-face material slots."""

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.mats = []

    def _slot(self, key):
        mat = MAT[key]
        if mat not in self.mats:
            self.mats.append(mat)
        return self.mats.index(mat)

    def _tag(self, verts, key, smooth=False):
        idx = self._slot(key)
        faces = {f for v in verts for f in v.link_faces}
        for f in faces:
            f.material_index = idx
            f.smooth = smooth

    def box(self, center, size, key, rot=(0, 0, 0)):
        m = (Matrix.Translation(Vector(center))
             @ Euler([math.radians(a) for a in rot]).to_matrix().to_4x4()
             @ Matrix.Diagonal((*size, 1.0)))
        res = bmesh.ops.create_cube(self.bm, size=1.0, matrix=m)
        self._tag(res["verts"], key)

    def cyl(self, center, radius, depth, key, axis="Z", segs=16, radius2=None, rot_extra=None, smooth=False):
        r = {"Z": Euler((0, 0, 0)), "X": Euler((0, math.pi / 2, 0)), "Y": Euler((math.pi / 2, 0, 0))}[axis]
        m = Matrix.Translation(Vector(center)) @ r.to_matrix().to_4x4()
        if rot_extra is not None:
            m = Matrix.Translation(Vector(center)) @ rot_extra @ r.to_matrix().to_4x4()
        res = bmesh.ops.create_cone(self.bm, cap_ends=True, cap_tris=False, segments=segs,
                                    radius1=radius, radius2=radius if radius2 is None else radius2,
                                    depth=depth, matrix=m)
        self._tag(res["verts"], key, smooth)

    def revolve_x(self, profile, key, segs=24, smooth=True, closed=False):
        """profile: list of (x, r) from back to front. r == 0 makes a pointed tip."""
        rings = []
        for x, r in profile:
            if r <= 1e-6:
                rings.append([self.bm.verts.new((x, 0, 0))])
            else:
                ring = []
                for i in range(segs):
                    a = 2 * math.pi * i / segs + math.pi / segs
                    ring.append(self.bm.verts.new((x, r * math.cos(a), r * math.sin(a))))
                rings.append(ring)
        new_faces = []
        pairs = list(zip(rings, rings[1:])) + ([(rings[-1], rings[0])] if closed else [])
        for ra, rb in pairs:
            if len(ra) == 1:
                for i in range(segs):
                    new_faces.append(self.bm.faces.new((ra[0], rb[(i + 1) % segs], rb[i])))
            elif len(rb) == 1:
                for i in range(segs):
                    new_faces.append(self.bm.faces.new((ra[i], ra[(i + 1) % segs], rb[0])))
            else:
                for i in range(segs):
                    j = (i + 1) % segs
                    new_faces.append(self.bm.faces.new((ra[i], ra[j], rb[j], rb[i])))
        for ring in (() if closed else (rings[0], rings[-1])):
            if len(ring) > 2:
                new_faces.append(self.bm.faces.new(ring))
        idx = self._slot(key)
        for f in new_faces:
            f.material_index = idx
            f.smooth = smooth

    def fin(self, x_root, x_tip, r_root, r_tip, angle_deg, thick, key):
        """Tapered fin: x_root/x_tip are (back, front) pairs; built in the (x, radial) plane."""
        a = math.radians(angle_deg)
        def p(x, r, t):
            return (x, r * math.cos(a) - t * math.sin(a), r * math.sin(a) + t * math.cos(a))
        quad = [(x_root[0], r_root), (x_root[1], r_root), (x_tip[1], r_tip), (x_tip[0], r_tip)]
        side_a = [self.bm.verts.new(p(x, r, -thick / 2)) for x, r in quad]
        side_b = [self.bm.verts.new(p(x, r, thick / 2)) for x, r in quad]
        faces = [self.bm.faces.new(side_a), self.bm.faces.new(list(reversed(side_b)))]
        for i in range(4):
            j = (i + 1) % 4
            faces.append(self.bm.faces.new((side_a[i], side_b[i], side_b[j], side_a[j])))
        idx = self._slot(key)
        for f in faces:
            f.material_index = idx

    def finish(self, location=(0, 0, 0)):
        bmesh.ops.recalc_face_normals(self.bm, faces=self.bm.faces)
        me = bpy.data.meshes.new(self.name)
        self.bm.to_mesh(me)
        self.bm.free()
        for m in self.mats:
            me.materials.append(m)
        ob = bpy.data.objects.new(self.name, me)
        ob.location = location
        bpy.context.collection.objects.link(ob)
        return ob


UCX = {}  # mesh name -> list of collision objects


def ucx(owner, center, size, rot=(0, 0, 0)):
    lst = UCX.setdefault(owner, [])
    b = Builder(f"UCX_{owner}_{len(lst):02d}")
    b.box(center, size, "Metal", rot)
    b.mats = []  # collision needs no material
    ob = b.finish()
    ob.display_type = "WIRE"
    ob.hide_render = True
    lst.append(ob)
    return ob


# ================================================================ dimensions
HULL_R = 2.4            # outer hull radius
CYL_X = 6.3             # straight hull section spans -CYL_X..+CYL_X
NOSE_X = 8.0            # tip of the nose
TAIL_X = -8.0           # tail cone end
IN_X0, IN_X1 = -6.0, 6.0  # interior length
IN_W = 1.7              # interior half width
FLOOR_Z, CEIL_Z = -1.2, 1.4
T = 0.15                # wall thickness
CH = 0.35               # chamfer leg
DIV_HELM = 2.5          # wall between sonar and helm
DIV_ENGINE = -1.5       # wall between engine and sonar
DOOR_W, DOOR_TOP = 0.75, 1.0  # half width, top z (door = 1.5 m x 2.2 m; capsule is 1.1 m)

# ================================================================ exterior hull
hull = Builder("SM_Sub_Hull")
profile = []
tail_r = 0.45
for i in range(7):  # tail taper
    t = 1 - i / 6
    x = -CYL_X - t * (CYL_X + TAIL_X) * -1
    x = -CYL_X - t * (-TAIL_X - CYL_X)
    profile.append((x, HULL_R - (HULL_R - tail_r) * t ** 1.7))
profile.append((0.0, HULL_R))
for i in range(0, 9):  # elliptical nose
    t = i / 8
    x = CYL_X + t * (NOSE_X - CYL_X)
    r = HULL_R * math.sqrt(max(0.0, 1 - t * t))
    profile.append((x, r))
hull.revolve_x(profile, "Hull", segs=24, smooth=True)

# hull bands
for bx in (-4.8, -1.0, 3.6):
    hull.revolve_x([(bx - 0.08, HULL_R - 0.1), (bx - 0.08, HULL_R + 0.08), (bx + 0.08, HULL_R + 0.08), (bx + 0.08, HULL_R - 0.1)],
                   "HullDark", segs=24, smooth=False, closed=True)

# sail / conning tower (elliptical)
sail_m = Matrix.Diagonal((1.9, 0.6, 1.0, 1.0)).to_3x3().to_4x4()
res = bmesh.ops.create_cone(hull.bm, cap_ends=True, cap_tris=False, segments=14, radius1=1.0, radius2=0.85,
                            depth=1.3, matrix=Matrix.Translation((1.6, 0, 2.95)) @ sail_m)
hull._tag(res["verts"], "Hull")
hull.box((1.6, 0, 3.62), (2.6, 0.5, 0.06), "HullDark")            # sail cap trim
hull.box((2.0, 0, 2.95), (0.9, 3.0, 0.08), "HullDark")            # sail planes
hull.cyl((0.9, 0, 3.75), 0.07, 0.6, "Metal")                      # periscope
hull.cyl((0.9, 0, 4.05), 0.1, 0.12, "Metal", axis="X")

# deck hatch + keel
hull.cyl((-3.0, 0, 2.38), 0.45, 0.14, "HullDark", segs=12)
hull.cyl((-3.0, 0, 2.47), 0.3, 0.06, "Metal", segs=12)
hull.box((0.0, 0, -2.4), (11.0, 0.25, 0.25), "HullDark")

# tail fins (cross)
for ang in (0, 90, 180, 270):
    hull.fin(x_root=(-7.4, -5.9), x_tip=(-7.9, -7.0), r_root=1.2, r_tip=2.7, angle_deg=ang, thick=0.12, key="HullDark")

# propeller shaft
hull.cyl((TAIL_X - 0.08, 0, 0), 0.18, 0.3, "Metal", axis="X", segs=12)
hull_ob = hull.finish()

# propeller (own pivot at hub so it can spin)
PROP_X = TAIL_X - 0.38
prop = Builder("SM_Sub_Propeller")
prop.cyl((0, 0, 0), 0.28, 0.4, "Brass", axis="X", segs=12)
prop.cyl((-0.3, 0, 0), 0.28, 0.25, "Brass", axis="X", segs=12, radius2=0.05)
for k in range(5):
    a = 2 * math.pi * k / 5
    m = (Euler((a, 0, 0)).to_matrix().to_4x4()
         @ Matrix.Translation((0, 0, 0.75))
         @ Euler((0, 0, math.radians(35))).to_matrix().to_4x4()
         @ Matrix.Diagonal((0.06, 0.42, 1.0, 1.0)))
    r = bmesh.ops.create_cube(prop.bm, size=1.0, matrix=m)
    prop._tag(r["verts"], "Brass")
prop_ob = prop.finish(location=(PROP_X, 0, 0))

# ================================================================ interior shell
inner = Builder("SM_Sub_Interior")
L = IN_X1 - IN_X0
CX = (IN_X0 + IN_X1) / 2
MID_Z = (FLOOR_Z + CEIL_Z) / 2
H = CEIL_Z - FLOOR_Z

def solid(center, size, key, rot=(0, 0, 0), collide=True):
    inner.box(center, size, key, rot)
    if collide:
        ucx("SM_Sub_Interior", center, size, rot)

# floor / ceiling / side walls / end walls
solid((CX, 0, FLOOR_Z - T / 2), (L + 2 * T, 2 * IN_W + 2 * T, T), "Floor")
# Upper corners are trimmed (the chamfers close them) so nothing pokes through the round hull.
CEIL_HW = IN_W - CH + 0.1          # ceiling half width
WALL_TOP = CEIL_Z - CH + 0.1       # side walls stop under the upper chamfer
solid((CX, 0, CEIL_Z + T / 2), (L + 2 * T, 2 * CEIL_HW, T), "Wall")
for s in (1, -1):
    z0, z1 = FLOOR_Z - T, WALL_TOP
    solid((CX, s * (IN_W + T / 2), (z0 + z1) / 2), (L + 2 * T, T, z1 - z0), "Wall")
for ex, sgn in ((IN_X1, 1), (IN_X0, -1)):
    x = ex + sgn * T / 2
    solid((x, 0, MID_Z), (T, 2 * CEIL_HW, H + 2 * T), "Wall")
    solid((x, 0, (FLOOR_Z - T + WALL_TOP) / 2), (T, 2 * IN_W + 2 * T, WALL_TOP - FLOOR_Z + T), "Wall")

# 45 degree chamfers in the four long corners (gives the rounded sub feel)
cw = CH * math.sqrt(2)
o = T / 2 / math.sqrt(2)
for s in (1, -1):
    # lower
    solid((CX, s * (IN_W - CH / 2 + o), FLOOR_Z + CH / 2 - o), (L, cw, T), "Metal", rot=(s * 45, 0, 0))
    # upper
    solid((CX, s * (IN_W - CH / 2 + o), CEIL_Z - CH / 2 + o), (L, cw, T), "Wall", rot=(-s * 45, 0, 0))

# divider walls with doorways
for dx in (DIV_HELM, DIV_ENGINE):
    side_w = IN_W - DOOR_W
    for s in (1, -1):
        solid((dx, s * (DOOR_W + side_w / 2), MID_Z), (T, side_w, H), "Wall")
    solid((dx, 0, (DOOR_TOP + CEIL_Z) / 2), (T, 2 * DOOR_W, CEIL_Z - DOOR_TOP), "Wall")
    # yellow door frame (decor)
    for s in (1, -1):
        inner.box((dx, s * (DOOR_W + 0.04), (FLOOR_Z + DOOR_TOP + 0.08) / 2), (0.26, 0.12, DOOR_TOP + 0.08 - FLOOR_Z), "Warning")
    inner.box((dx, 0, DOOR_TOP + 0.03), (0.26, 2 * DOOR_W - 0.04, 0.1), "Warning")
    # door sill
    inner.box((dx, 0, FLOOR_Z + 0.01), (0.3, 2 * DOOR_W - 0.04, 0.02), "Warning")

# ribs (decor, no collision)
for rx in (-5.0, -3.5, -0.4, 1.4, 3.5, 5.0):
    for s in (1, -1):
        inner.box((rx, s * (IN_W - 0.04), MID_Z), (0.12, 0.08, H - 2 * CH), "Metal")
    inner.box((rx, 0, CEIL_Z - 0.04), (0.12, 2 * (IN_W - CH), 0.08), "Metal")

# ceiling pipes + light fixtures
for s in (1, -1):
    inner.cyl((CX, s * 1.15, CEIL_Z - 0.2), 0.07, L, "Metal", axis="X", segs=8)
for lx in (4.25, 0.5, -3.75):
    inner.box((lx, 0, CEIL_Z - 0.03), (0.9, 0.25, 0.06), "Light")
    inner.box((lx, 0, CEIL_Z - 0.01), (1.0, 0.35, 0.03), "Metal")

# floor seams along the walkway
for sx in (-4.5, -3.0, 0.0, 1.5, 4.0, 5.2):
    inner.box((sx, 0, FLOOR_Z + 0.005), (0.04, 2 * (IN_W - CH), 0.01), "Metal")

# lockers in the sonar room (opposite the sonar desk)
solid((0.5, -(IN_W - 0.28), FLOOR_Z + 1.0), (1.6, 0.5, 2.0), "Console")
for k in range(3):
    inner.box((0.5 - 0.53 + k * 0.53, -(IN_W - 0.54), FLOOR_Z + 1.0), (0.48, 0.03, 1.85), "Metal")
inner_ob = inner.finish()

# ================================================================ helm console (front room)
helm = Builder("SM_Sub_HelmConsole")
hx = IN_X1 - 0.4
helm.box((hx, 0, FLOOR_Z + 0.45), (0.7, 2.4, 0.9), "Console")
helm.box((hx - 0.05, 0, FLOOR_Z + 0.93), (0.62, 2.4, 0.08), "Metal", rot=(0, -25, 0))
helm.box((IN_X1 - 0.08, 0, 0.55), (0.12, 2.6, 1.2), "Console")
helm.box((IN_X1 - 0.145, 0, 0.6), (0.02, 1.1, 0.65), "ScreenGreen")
for s in (1, -1):
    helm.box((IN_X1 - 0.145, s * 0.9, 0.7), (0.02, 0.5, 0.4), "ScreenAmber")
    for gy in (0.65, 1.0):
        helm.cyl((IN_X1 - 0.15, s * gy, 0.15), 0.08, 0.03, "ScreenAmber", axis="X", segs=12)
# yoke
helm.cyl((hx - 0.5, 0, FLOOR_Z + 1.05), 0.04, 0.4, "Metal", axis="X", segs=8)
helm.box((hx - 0.72, 0, FLOOR_Z + 1.05), (0.05, 0.6, 0.05), "Metal")
for s in (1, -1):
    helm.cyl((hx - 0.72, s * 0.3, FLOOR_Z + 1.05), 0.035, 0.2, "Console", segs=8)
# throttle lever
helm.box((hx - 0.2, 0.9, FLOOR_Z + 0.95), (0.25, 0.15, 0.1), "Metal")
helm.box((hx - 0.25, 0.9, FLOOR_Z + 1.12), (0.04, 0.04, 0.35), "Metal", rot=(0, 20, 0))
helm.cyl((hx - 0.31, 0.9, FLOOR_Z + 1.29), 0.05, 0.08, "Red", segs=8)
# warning stripe on base
helm.box((hx - 0.355, 0, FLOOR_Z + 0.06), (0.02, 2.4, 0.1), "Warning")
helm_ob = helm.finish()
ucx("SM_Sub_HelmConsole", (hx, 0, FLOOR_Z + 0.48), (0.75, 2.4, 0.96))
ucx("SM_Sub_HelmConsole", (IN_X1 - 0.08, 0, 0.55), (0.12, 2.6, 1.2))

# ================================================================ sonar console (middle room, +Y wall)
son = Builder("SM_Sub_SonarConsole")
sx, sy = 0.5, IN_W - 0.35
son.box((sx, sy, FLOOR_Z + 0.5), (1.8, 0.7, 1.0), "Console")
son.box((sx, IN_W - 0.06, FLOOR_Z + 1.6), (1.8, 0.12, 1.2), "Console")
son.cyl((sx, IN_W - 0.13, FLOOR_Z + 1.65), 0.5, 0.05, "Metal", axis="Y", segs=20)
son.cyl((sx, IN_W - 0.16, FLOOR_Z + 1.65), 0.42, 0.04, "ScreenGreen", axis="Y", segs=20)
for s in (1, -1):
    son.box((sx + s * 0.68, IN_W - 0.13, FLOOR_Z + 1.65), (0.35, 0.02, 0.28), "ScreenAmber")
son.box((sx, sy - 0.15, FLOOR_Z + 1.03), (1.2, 0.3, 0.04), "Metal", rot=(15, 0, 0))
son.cyl((sx + 0.5, sy - 0.18, FLOOR_Z + 1.07), 0.06, 0.06, "Red", segs=10)   # PING button
son.box((sx - 0.94, sy, FLOOR_Z + 0.5), (0.04, 0.72, 1.0), "Warning")
son_ob = son.finish()
ucx("SM_Sub_SonarConsole", (sx, sy, FLOOR_Z + 0.5), (1.8, 0.7, 1.0))
ucx("SM_Sub_SonarConsole", (sx, IN_W - 0.06, FLOOR_Z + 1.6), (1.8, 0.12, 1.2))

# ================================================================ engine (back room)
eng = Builder("SM_Sub_Engine")
ex0, ex1 = IN_X0 + 0.4, DIV_ENGINE - 1.9
ecx, ez = (ex0 + ex1) / 2, FLOOR_Z + 0.8
elen = ex1 - ex0
eng.box((ecx, 0, FLOOR_Z + 0.1), (elen - 0.2, 0.8, 0.2), "Warning")
eng.cyl((ecx, 0, ez), 0.5, elen, "Metal", axis="X", segs=16)
for bx in (ex0 + 0.1, ecx, ex1 - 0.1):
    eng.cyl((bx, 0, ez), 0.55, 0.12, "HullDark", axis="X", segs=16)
eng.cyl((ex1 + 0.1, 0, ez), 0.42, 0.1, "Brass", axis="X", segs=16)           # flywheel
eng.cyl(((IN_X0 + ex0) / 2, 0, ez), 0.15, ex0 - IN_X0, "Metal", axis="X", segs=10)  # shaft into wall
for s in (1, -1):
    eng.cyl((ecx - 0.4, s * 0.25, (ez + 0.5 + CEIL_Z) / 2), 0.08, CEIL_Z - (ez + 0.5), "Metal", segs=8)
    eng.cyl((ecx + 0.5, s * 0.3, ez + 0.5), 0.11, 0.25, "HullDark", segs=10)
eng_ob = eng.finish()
ucx("SM_Sub_Engine", (ecx - 0.05, 0, FLOOR_Z + 0.68), (elen + 0.2, 1.12, 1.36))

# ================================================================ power panel (engine room, -Y wall)
pp = Builder("SM_Sub_PowerPanel")
px = DIV_ENGINE - 0.9
pp.box((px, -(IN_W - 0.12), 0.2), (1.0, 0.24, 1.4), "Console")
pp.box((px, -(IN_W - 0.245), 0.2), (0.9, 0.01, 1.3), "Metal")
for row, z in enumerate((0.45, 0.05)):
    for k in range(4):
        x = px - 0.3 + k * 0.2
        pp.box((x, -(IN_W - 0.29), z), (0.08, 0.08, 0.16), "Warning" if row == 0 else "Metal")
        pp.cyl((x, -(IN_W - 0.26), z + 0.18), 0.025, 0.02, "ScreenGreen" if k % 2 == 0 else "Red", axis="Y", segs=8)
pp.box((px, -(IN_W - 0.27), 0.75), (0.6, 0.02, 0.12), "ScreenAmber")
pp_ob = pp.finish()
ucx("SM_Sub_PowerPanel", (px, -(IN_W - 0.15), 0.2), (1.0, 0.3, 1.4))

MESHES = [hull_ob, inner_ob, helm_ob, son_ob, eng_ob, pp_ob]
print("built:", [o.name for o in MESHES], "+ propeller", "| UCX:", {k: len(v) for k, v in UCX.items()})

# ================================================================ save + export
blend_path = os.path.join(OUT, "Submarine.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend_path)

def select_only(objs):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]

fbx_common = dict(use_selection=True, object_types={"MESH"}, apply_unit_scale=True,
                  apply_scale_options="FBX_SCALE_ALL", mesh_smooth_type="FACE",
                  add_leaf_bones=False, bake_anim=False, axis_forward="-Z", axis_up="Y")

all_ucx = [o for lst in UCX.values() for o in lst]
select_only(MESHES + all_ucx)
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "SM_Submarine.fbx"), **fbx_common)

# propeller: export with pivot at the hub
prop_ob.location = (0, 0, 0)
select_only([prop_ob])
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "SM_Sub_Propeller.fbx"), **fbx_common)
prop_ob.location = (PROP_X, 0, 0)
bpy.ops.wm.save_mainfile()

# stats for the report
for o in MESHES + [prop_ob]:
    print(f"{o.name:24s} tris={sum(len(p.vertices) - 2 for p in o.data.polygons):6d} "
          f"dims(cm)={tuple(round(d * 100) for d in o.dimensions)}")
print("PROP_X_cm", round(PROP_X * 100))
