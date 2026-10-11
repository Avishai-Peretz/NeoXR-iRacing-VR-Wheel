"""Exports baked rigid parts as standard glTF 2.0. Measurement correction is NEO X opt-in only."""
import json, struct
from pathlib import Path

# Control slots 8.. in runtime order (src/animation.hpp). 'joistick_right' matches the model's node name.
CONTROLS = ['left_shifter', 'right_shifter', 'left_clutch_lever', 'right_clutch_lever',
            'B9_B10_rotary_knob', 'B11_B12_rotary_knob', 'B37_B38_rotary_knob', 'B39_B40_rotary_knob', 'joystick_left', 'joistick_right']
MAX_VERTICES, MAX_INDICES = 2_000_000, 6_000_000


def control_slot(name):
    """The animated control a part belongs to, or -1 for static parts."""
    if name.startswith('B') and name.endswith('_push_btn'):
        slot = int(name.split('_')[0][1:]) - 1
        if slot not in range(8): raise ValueError('Invalid push button ' + name)
        return slot
    return 8 + CONTROLS.index(name) if name in CONTROLS else -1


# The source model is about 5% small and its top is too narrow for its bottom. Measured on the real wheel:
# B7-B4 centres 161 mm, B6-B2 centres 127 mm, 318 mm wide at hub height, 197.5 mm tall overall.
# x scales by WIDEN + WIDEN_PER_METRE*y (wider toward the top), y by TALLER and depth by DEEPER.
WIDEN, WIDEN_PER_METRE, TALLER, DEEPER = 1.0480, 0.399, 1.0836, 1.0507


def measured_shape(row):
    """Moves one vertex row (position, normal, ...) to the real wheel's proportions, in place."""
    x, y, z, nx, ny, nz = row[0], row[1], row[2], row[3], row[4], row[5]
    sx = WIDEN + WIDEN_PER_METRE * y
    row[0], row[1], row[2] = x * sx, y * TALLER, z * DEEPER
    # Normals take the inverse transpose of the mapping's Jacobian [[sx, k*x, 0], [0, TALLER, 0], [0, 0, DEEPER]].
    mx = nx / sx
    my = (ny - WIDEN_PER_METRE * x * mx) / TALLER
    mz = nz / DEEPER
    length = (mx * mx + my * my + mz * mz) ** .5 or 1
    row[3], row[4], row[5] = mx / length, my / length, mz / length


def part_bounds(vertices, indices, part):
    used = {indices[i] for i in range(part['index_start'], part['index_start'] + part['index_count'])}
    points = [vertices[i] for i in used]
    return [[float(min(p[k] for p in points)) for k in range(3)], [float(max(p[k] for p in points)) for k in range(3)]]


def paddle_pivot(control, bounds):
    """Approximate hinge for slots 8-11: inner edge, middle height, front edge; w is the swing direction."""
    inner = min([bounds[0][0], bounds[1][0]], key=abs)
    return [inner, (bounds[0][1] + bounds[1][1]) / 2, bounds[1][2], 1 if control in [8, 10] else -1]



def write(destination, vertices, indices, parts, images, source, measured=False):
    if not len(vertices) or len(vertices)>MAX_VERTICES or len(indices)>MAX_INDICES:
        raise ValueError('Asset exceeds runtime limits')
    if measured:
        for row in vertices: measured_shape(row)
    dest=Path(destination); dest.mkdir(parents=True,exist_ok=True)
    blob=bytearray(); doc={'asset':{'version':'2.0','generator':'NeoXR glTF exporter'},
        'scene':0,'scenes':[{'nodes':[]}],'nodes':[],'meshes':[],'materials':[],
        'buffers':[{'uri':'neo-x.bin','byteLength':0}],'bufferViews':[],'accessors':[],
        'images':[{'uri':'wheel-'+k+'.png'} for k in ('basecolor','normal','orm')],
        'samplers':[{'wrapS':33071,'wrapT':33071,'minFilter':9987,'magFilter':9729}],
        'textures':[{'source':i,'sampler':0} for i in range(3)]}
    def accessor(rows,kind,component=5126,bounds=False):
        while len(blob)%4: blob.append(0)
        start=len(blob); fmt='<'+('I' if component==5125 else 'f')*len(rows[0])
        for row in rows: blob.extend(struct.pack(fmt,*row))
        view=len(doc['bufferViews']); doc['bufferViews'].append({'buffer':0,'byteOffset':start,'byteLength':len(blob)-start})
        a={'bufferView':view,'componentType':component,'count':len(rows),'type':kind}
        if bounds: a.update(min=[min(r[k] for r in rows) for k in range(3)],max=[max(r[k] for r in rows) for k in range(3)])
        doc['accessors'].append(a); return len(doc['accessors'])-1
    materials={}
    for part in parts:
        groups={}
        for i in range(part['index_start'],part['index_start']+part['index_count'],3):
            tri=[int(x) for x in indices[i:i+3]]; signature=tuple(float(x) for x in vertices[tri[0]][8:16])
            if any(tuple(float(x) for x in vertices[t][8:16])!=signature for t in tri):
                raise ValueError('Bake material boundaries into separate vertices before exporting')
            groups.setdefault(signature,[]).extend(tri)
        primitives=[]
        for signature,ix in groups.items():
            if signature not in materials:
                materials[signature]=len(doc['materials']); base=signature[:3]; emission=signature[3:6]; metal,rough=signature[6:8]
                mat={'name':'Material '+str(len(materials)), 'doubleSided':True,
                    'pbrMetallicRoughness':{'baseColorFactor':list(base)+[1],'baseColorTexture':{'index':0},
                    'metallicFactor':metal,'roughnessFactor':rough,'metallicRoughnessTexture':{'index':2}},
                    'normalTexture':{'index':1},'occlusionTexture':{'index':2},'emissiveFactor':list(emission)}
                doc['materials'].append(mat)
            used=list(dict.fromkeys(ix)); remap={v:i for i,v in enumerate(used)}; rows=[vertices[v] for v in used]
            attrs={'POSITION':accessor([r[:3] for r in rows],'VEC3',bounds=True),
                'NORMAL':accessor([r[3:6] for r in rows],'VEC3'),'TEXCOORD_0':accessor([r[6:8] for r in rows],'VEC2')}
            primitives.append({'attributes':attrs,'indices':accessor([[remap[i]] for i in ix],'SCALAR',5125),'material':materials[signature]})
        doc['scenes'][0]['nodes'].append(len(doc['nodes']))
        doc['nodes'].append({'name':part['name'],'mesh':len(doc['meshes'])})
        doc['meshes'].append({'name':part['name'],'primitives':primitives})
    doc['buffers'][0]['byteLength']=len(blob)
    (dest/'neo-x.bin').write_bytes(blob)
    (dest/'neo-x.gltf').write_text(json.dumps(doc,indent=2))
    for kind,data in images.items(): (dest/('wheel-'+kind+'.png')).write_bytes(data)
    report={'source':str(source),'vertex_count':len(vertices),'triangle_count':len(indices)//3,
        'width_m':float(max(v[0] for v in vertices)-min(v[0] for v in vertices)), 'parts':parts}
    if measured: report['measured_shape']=[WIDEN,WIDEN_PER_METRE,TALLER,DEEPER]
    (dest/'wheel-parts.json').write_text(json.dumps(report,indent=2))
    return report
