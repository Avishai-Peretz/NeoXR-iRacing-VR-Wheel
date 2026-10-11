"""Bakes a wheel .blend into standard glTF plus three texture atlases.

    blender -b andvanced_2.blend --python tools/prepare_wheel.py -- <output folder> [options]

The .blend is never saved. Every visible mesh part is simplified to a triangle budget, all parts share one
UV atlas, and the original materials are baked onto it from the full-detail model:
  basecolor  material Base Color          (sRGB)
  normal     shading normal, tangent space (OpenGL), so lost geometry and material normal maps survive
  orm        R ambient occlusion, G roughness, B metallic
LED caps keep their emission as per-vertex factors, so the runtime flash can still drive them.
"""
import argparse, math, sys, time
from pathlib import Path
import bpy
import numpy as np
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gltf_asset

HUB = 'simagic_screws_cover'  # the runtime origin; the wheel spins about this part's centre
# The current VR orientation: the original turned half a turn about the face normal.
RUNTIME = Matrix.Rotation(math.pi, 4, 'Z')
# Materials whose shader is a node group without a top-level Principled BSDF: values to bake instead.
GROUP_FALLBACK = {'Metallic': 0.5}
# The low copy sits inside the original it is baked from and must not shadow or occlude it.
RAY_FLAGS = ('visible_diffuse', 'visible_glossy', 'visible_transmission', 'visible_volume_scatter', 'visible_shadow')


def arguments():
    p = argparse.ArgumentParser(prog='prepare_wheel.py')
    p.add_argument('output')
    p.add_argument('--size', type=int, default=4096, help='atlas size in pixels')
    p.add_argument('--triangles', type=int, default=600_000, help='triangle budget for the whole wheel')
    p.add_argument('--samples', type=int, default=16, help='bake samples per pixel (AO uses 4x)')
    p.add_argument('--blend', help='also save the prepared low-poly scene here, to inspect in Blender')
    p.add_argument('--measured-neo-x', action='store_true', help='apply the measured NEO X correction to its original source only')
    return p.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])


def log(*text): print('[prepare]', *text, flush=True)


# ---------------------------------------------------------------------------------------------------
# Geometry
# ---------------------------------------------------------------------------------------------------

def visible_parts():
    parts = [o for o in bpy.context.scene.objects if o.type == 'MESH' and not o.hide_render and o.visible_get()]
    if not any(o.name == HUB for o in parts): raise ValueError(HUB + ' not found; is this the NEO X model?')
    return parts


def triangle_counts(parts):
    deps = bpy.context.evaluated_depsgraph_get()
    counts = {}
    for o in parts:
        me = o.evaluated_get(deps).to_mesh()
        counts[o.name] = sum(len(p.vertices) - 2 for p in me.polygons)
        o.evaluated_get(deps).to_mesh_clear()
    return counts


def allocate(counts, budget, exponent=0.6):
    """Shares the budget by count**exponent, so small detailed parts lose less than big plain ones.
    Parts that fit keep every triangle and give the rest back to the others."""
    targets, remaining, left = {}, dict(counts), budget
    while remaining:
        weight = {n: c ** exponent for n, c in remaining.items()}
        total = sum(weight.values())
        fits = [n for n in remaining if counts[n] <= left * weight[n] / total]
        if not fits:
            for n in remaining: targets[n] = max(64, int(left * weight[n] / total))
            break
        for n in fits: targets[n] = counts[n]; left -= counts[n]; del remaining[n]
    return targets


def simplified_copy(source, target, index, collection, material):
    """World-space copy of the evaluated part, decimated to `target` triangles, tagged with its part index."""
    deps = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(source.evaluated_get(deps))
    mesh.transform(source.matrix_world)
    low = bpy.data.objects.new(source.name + '_low', mesh)
    collection.objects.link(low)
    # Unwrapped at full detail, where the surface is clean; decimation then keeps the seams.
    while mesh.uv_layers: mesh.uv_layers.remove(mesh.uv_layers[0])
    atlas = mesh.uv_layers.new(name='atlas')
    atlas.active = atlas.active_render = True
    with editing(low):
        bpy.ops.mesh.remove_doubles(threshold=1e-5)  # CAD exports leave each face loose; weld so islands can form
        # A wide angle keeps small chamfers on their neighbouring face instead of in islands of their own.
        bpy.ops.uv.smart_project(angle_limit=math.radians(80), island_margin=0, area_weight=0, scale_to_bounds=False)
    triangles = sum(len(p.vertices) - 2 for p in mesh.polygons)
    if target < triangles:
        decimate = low.modifiers.new('Decimate', 'DECIMATE')
        decimate.ratio = target / triangles
        decimate.use_collapse_triangulate = True
        deps = bpy.context.evaluated_depsgraph_get()
        reduced = bpy.data.meshes.new_from_object(low.evaluated_get(deps))
        low.modifiers.clear()
        low.data = reduced
        bpy.data.meshes.remove(mesh)
    low.data.materials.clear()
    low.data.materials.append(material)
    tag = low.data.attributes.new('neo_part', 'INT', 'FACE')
    tag.data.foreach_set('value', [index] * len(low.data.polygons))
    return low


def join(objects):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects: o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    joined = bpy.context.view_layer.objects.active
    joined.name = 'NeoXR_wheel_low'
    return joined


class editing:
    """Edit mode on one object with every face revealed and selected."""

    def __init__(self, obj): self.obj = obj

    def __enter__(self):
        bpy.ops.object.select_all(action='DESELECT')
        self.obj.select_set(True)
        bpy.context.view_layer.objects.active = self.obj
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.reveal(select=False)
        bpy.ops.mesh.select_all(action='SELECT')

    def __exit__(self, *error): bpy.ops.object.mode_set(mode='OBJECT')


def pack(obj, margin):
    """Gives every part the same texel density and packs all islands into one atlas."""
    me = obj.data
    me.shade_smooth()
    me.set_sharp_from_angle(angle=math.radians(40))
    with editing(obj):
        bpy.ops.uv.average_islands_scale()
        bpy.ops.uv.pack_islands(shape_method='CONCAVE', rotate=True, margin_method='FRACTION', margin=margin)
    uv = np.empty(len(me.loops) * 2); me.uv_layers['atlas'].data.foreach_get('uv', uv)
    if uv.min() < -1e-4 or uv.max() > 1 + 1e-4: raise RuntimeError('Atlas unwrap left UVs in %.3f..%.3f' % (uv.min(), uv.max()))


def atlas_coverage(obj):
    me = obj.data
    me.calc_loop_triangles()
    loops = np.empty(len(me.loop_triangles) * 3, dtype=np.int64); me.loop_triangles.foreach_get('loops', loops)
    uv = np.empty(len(me.loops) * 2); me.uv_layers['atlas'].data.foreach_get('uv', uv)
    a, b, c = (uv.reshape(-1, 2)[loops].reshape(-1, 3, 2)[:, i] for i in range(3))
    return float(np.abs(np.cross(b - a, c - a)).sum() / 2)


# ---------------------------------------------------------------------------------------------------
# Baking
# ---------------------------------------------------------------------------------------------------

def use_gpu(scene):
    prefs = bpy.context.preferences.addons['cycles'].preferences
    for kind in ('OPTIX', 'CUDA', 'HIP', 'ONEAPI', 'METAL'):
        try: prefs.compute_device_type = kind
        except TypeError: continue
        prefs.get_devices()
        devices = [d for d in prefs.devices if d.type == kind]
        if devices:
            for d in prefs.devices: d.use = d.type == kind
            scene.cycles.device = 'GPU'
            return kind + ' ' + ', '.join(d.name for d in devices)
    scene.cycles.device = 'CPU'
    return 'CPU'


def find_principled(tree):
    """(tree, Principled node, output socket the shader reaches) for a material, looking one group deep."""
    output = next((n for n in tree.nodes if n.bl_idname == 'ShaderNodeOutputMaterial' and n.is_active_output), None) \
        or next((n for n in tree.nodes if n.bl_idname == 'ShaderNodeOutputMaterial'), None)
    principled = next((n for n in tree.nodes if n.bl_idname == 'ShaderNodeBsdfPrincipled'), None)
    if principled: return tree, principled, output.inputs['Surface']
    for group in (n for n in tree.nodes if n.bl_idname == 'ShaderNodeGroup' and n.node_tree):
        inner = next((n for n in group.node_tree.nodes if n.bl_idname == 'ShaderNodeBsdfPrincipled'), None)
        out = next((n for n in group.node_tree.nodes if n.bl_idname == 'NodeGroupOutput'), None)
        if inner and out: return group.node_tree, inner, out.inputs[0]
    return None, None, output.inputs['Surface'] if output else None


class EmitChannel:
    """Temporarily routes one Principled input of every source material into an Emission shader, so an
    EMIT bake records the raw material value. Restores the original links on exit."""

    def __init__(self, materials, socket):
        self.materials, self.socket, self.undo = materials, socket, []

    def __enter__(self):
        done = set()
        for m in self.materials:
            tree, principled, target = find_principled(m.node_tree)
            if target is None or (tree and tree.as_pointer() in done): continue
            tree = tree or m.node_tree
            done.add(tree.as_pointer())
            emit = tree.nodes.new('ShaderNodeEmission')
            emit.inputs['Strength'].default_value = 1
            source = principled.inputs[self.socket] if principled else None
            if source is not None and source.is_linked:
                tree.links.new(source.links[0].from_socket, emit.inputs['Color'])
            else:
                value = source.default_value if source is not None else GROUP_FALLBACK.get(self.socket, 0.5)
                emit.inputs['Color'].default_value = tuple(value)[:3] + (1,) if hasattr(value, '__len__') else (value, value, value, 1)
            previous = target.links[0].from_socket if target.is_linked else None
            tree.links.new(emit.outputs['Emission'], target)
            self.undo.append((tree, emit, previous, target))
        return self

    def __exit__(self, *error):
        for tree, emit, previous, target in reversed(self.undo):
            tree.nodes.remove(emit)
            if previous: tree.links.new(previous, target)


def bake_image(name, size, colorspace):
    image = bpy.data.images.new(name, size, size, alpha=False, float_buffer=False)
    image.colorspace_settings.name = colorspace
    return image


def bake(kind, image, node, sources, low, settings, samples, **options):
    node.image = image
    bpy.context.scene.cycles.samples = samples
    bpy.ops.object.select_all(action='DESELECT')
    for o in sources: o.select_set(True)
    low.select_set(True)
    bpy.context.view_layer.objects.active = low
    started = time.time()
    bpy.ops.object.bake(type=kind, use_selected_to_active=True, cage_extrusion=settings['extrusion'],
                        max_ray_distance=settings['reach'], margin=settings['margin'], margin_type='EXTEND',
                        use_clear=True, target='IMAGE_TEXTURES', **options)
    log(kind, image.name, 'baked in', round(time.time() - started), 's, mean', pixels(image)[..., :3].mean((0, 1)).round(3))


def pixels(image):
    data = np.empty(image.size[0] * image.size[1] * 4, dtype=np.float32)
    image.pixels.foreach_get(data)
    return data.reshape(image.size[1], image.size[0], 4)


def png_bytes(image, path):
    image.filepath_raw = str(path)
    image.file_format = 'PNG'
    image.save()
    return Path(path).read_bytes()


# ---------------------------------------------------------------------------------------------------
# Export
# ---------------------------------------------------------------------------------------------------

def led_emission(source):
    """Emission colour times strength of a push button cap, or black for every other part."""
    if gltf_asset.control_slot(source.name) not in range(8): return (0.0, 0.0, 0.0)
    for slot in source.material_slots:
        tree, principled, _ = find_principled(slot.material.node_tree) if slot.material and slot.material.use_nodes else (None, None, None)
        if principled and not principled.inputs['Emission Color'].is_linked:
            c, s = principled.inputs['Emission Color'].default_value, principled.inputs['Emission Strength'].default_value
            return tuple(float(c[i] * s) for i in range(3))
    return (0.0, 0.0, 0.0)


def export_rows(low, sources, origin):
    """Unique runtime vertices, triangle indices grouped by part, and the part table."""
    me = low.data
    me.calc_loop_triangles()
    tri_loops = np.empty(len(me.loop_triangles) * 3, dtype=np.int64); me.loop_triangles.foreach_get('loops', tri_loops)
    tri_poly = np.empty(len(me.loop_triangles), dtype=np.int64); me.loop_triangles.foreach_get('polygon_index', tri_poly)
    loop_vertex = np.empty(len(me.loops), dtype=np.int64); me.loops.foreach_get('vertex_index', loop_vertex)
    co = np.empty(len(me.vertices) * 3, dtype=np.float64); me.vertices.foreach_get('co', co); co = co.reshape(-1, 3)
    normals = np.empty(len(me.loops) * 3, dtype=np.float64); me.corner_normals.foreach_get('vector', normals); normals = normals.reshape(-1, 3)
    uv = np.empty(len(me.loops) * 2, dtype=np.float64); me.uv_layers['atlas'].data.foreach_get('uv', uv); uv = uv.reshape(-1, 2)
    part_of_poly = np.empty(len(me.polygons), dtype=np.int64); me.attributes['neo_part'].data.foreach_get('value', part_of_poly)

    rotation = np.array(RUNTIME.to_3x3(), dtype=np.float64)
    position = (co - np.array(origin)) @ rotation.T
    normals = normals @ rotation.T
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)

    # Triangles sorted by part so every part is one contiguous index range.
    tri_part = part_of_poly[tri_poly]
    order = np.argsort(tri_part, kind='stable')
    corners = tri_loops.reshape(-1, 3)[order].reshape(-1)
    corner_part = np.repeat(tri_part[order], 3)

    controls = np.array([gltf_asset.control_slot(s.name) for s in sources], dtype=np.float64)
    emission = np.array([led_emission(s) for s in sources], dtype=np.float64)
    rows = np.zeros((len(corners), 17), dtype=np.float64)
    rows[:, 0:3] = position[loop_vertex[corners]]
    rows[:, 3:6] = normals[corners]
    rows[:, 6] = uv[corners, 0]
    rows[:, 7] = 1 - uv[corners, 1]  # image rows run top to bottom at runtime
    rows[:, 8:11] = 1
    rows[:, 11:14] = emission[corner_part]
    rows[:, 14:16] = 1
    rows[:, 16] = controls[corner_part]
    key = np.round(rows[:, :8] * [1e6, 1e6, 1e6, 1e4, 1e4, 1e4, 1e6, 1e6]).astype(np.int64)
    key = np.concatenate([key, corner_part[:, None]], axis=1)
    _, first, inverse = np.unique(key, axis=0, return_index=True, return_inverse=True)
    vertices = rows[first].astype(np.float32)
    indices = inverse.reshape(-1).astype(np.uint32)
    if not np.all(np.isfinite(vertices)): raise ValueError('Non-finite vertex')

    parts, sorted_parts = [], tri_part[order]
    for index, source in enumerate(sources):
        tris = np.nonzero(sorted_parts == index)[0]
        if not len(tris): raise ValueError(source.name + ' lost every triangle')
        points = rows[tris[0] * 3:(tris[-1] + 1) * 3, 0:3]
        parts.append({'name': source.name, 'control': int(controls[index]), 'index_start': int(tris[0] * 3),
                      'index_count': int(len(tris) * 3), 'bounds': [points.min(0).tolist(), points.max(0).tolist()]})
    return vertices, indices, parts


# ---------------------------------------------------------------------------------------------------

def main():
    args = arguments()
    out = Path(args.output).resolve(); out.mkdir(parents=True, exist_ok=True)
    scene = bpy.context.scene
    if bpy.context.object and bpy.context.object.mode != 'OBJECT': bpy.ops.object.mode_set(mode='OBJECT')

    sources = sorted(visible_parts(), key=lambda o: o.name)
    hub = next(o for o in sources if o.name == HUB)
    corners = [hub.matrix_world @ Vector(c) for c in hub.bound_box]
    origin = tuple((min(c[i] for c in corners) + max(c[i] for c in corners)) / 2 for i in range(3))
    counts = triangle_counts(sources)
    targets = allocate(counts, args.triangles)
    log(len(sources), 'parts,', sum(counts.values()), 'triangles ->', sum(targets.values()))
    for o in sources: log('  ', o.name, counts[o.name], '->', targets[o.name])

    collection = bpy.data.collections.new('NeoXR bake')
    scene.collection.children.link(collection)
    material = bpy.data.materials.new('NeoXR_bake')
    material.use_nodes = True
    node = material.node_tree.nodes.new('ShaderNodeTexImage')
    material.node_tree.nodes.active = node
    started = time.time()
    lows = [simplified_copy(o, targets[o.name], i, collection, material) for i, o in enumerate(sources)]
    low = join(lows)
    pack(low, margin=3 / args.size)
    log('Unwrapped and simplified in', round(time.time() - started), 's; atlas', round(100 * atlas_coverage(low)), '% used')
    for attr in RAY_FLAGS: setattr(low, attr, False)

    scene.render.engine = 'CYCLES'
    log('Bake device:', use_gpu(scene))
    scene.render.bake.use_selected_to_active = True
    scene.world.light_settings.distance = 0.02  # AO reach in metres: crevices, not the whole wheel
    settings = {'extrusion': 0.002, 'reach': 0.006, 'margin': 16}
    materials = {s.material for o in sources for s in o.material_slots if s.material and s.material.use_nodes}

    images = {}
    base = bake_image('wheel-basecolor', args.size, 'sRGB')
    with EmitChannel(materials, 'Base Color'): bake('EMIT', base, node, sources, low, settings, args.samples)
    rough = bake_image('wheel-roughness', args.size, 'Non-Color')
    with EmitChannel(materials, 'Roughness'): bake('EMIT', rough, node, sources, low, settings, args.samples)
    metal = bake_image('wheel-metallic', args.size, 'Non-Color')
    with EmitChannel(materials, 'Metallic'): bake('EMIT', metal, node, sources, low, settings, args.samples)
    normal = bake_image('wheel-normal', args.size, 'Non-Color')
    bake('NORMAL', normal, node, sources, low, settings, args.samples, normal_space='TANGENT', normal_r='POS_X', normal_g='POS_Y', normal_b='POS_Z')
    occlusion = bake_image('wheel-ao', args.size, 'Non-Color')
    bake('AO', occlusion, node, sources, low, settings, args.samples * 4)

    orm = bake_image('wheel-orm', args.size, 'Non-Color')
    packed = np.ones((args.size, args.size, 4), dtype=np.float32)
    packed[..., 0], packed[..., 1], packed[..., 2] = pixels(occlusion)[..., 0], pixels(rough)[..., 0], pixels(metal)[..., 0]
    orm.pixels.foreach_set(packed.reshape(-1))
    for kind, image in (('basecolor', base), ('normal', normal), ('orm', orm)):
        images[kind] = png_bytes(image, out / ('wheel-' + kind + '.png'))

    vertices, indices, parts = export_rows(low, sources, origin)
    gltf_asset.write(out, vertices, indices, parts, images, bpy.data.filepath, measured=args.measured_neo_x)
    if args.blend:
        for o in sources: o.hide_set(True)
        bpy.ops.wm.save_as_mainfile(filepath=str(Path(args.blend).resolve()), copy=True, compress=True)
        log('Saved', args.blend)


main()
