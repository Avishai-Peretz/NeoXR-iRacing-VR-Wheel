"""Convert the prepared, baked GLB to NeoXR's bounded binary asset. No dependencies.
Supported: static triangle meshes, embedded PNG atlases, FLOAT positions/normals/UV,
node TRS/matrix transforms, opaque metallic-roughness materials sharing three atlases.
This deliberately rejects unsupported files rather than silently losing their appearance.
"""
import argparse,json,math,struct
from pathlib import Path
# Control slots 8.. in runtime order (src/animation.hpp). 'joistick_right' matches the model's node name.
CONTROLS=['left_shifter','right_shifter','left_clutch_lever','right_clutch_lever',
 'B9_B10_rotary_knob','B11_B12_rotary_knob','B37_B38_rotary_knob','B39_B40_rotary_knob','joystick_left','joistick_right']

def matmul(a,b):return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def local(node):
 if 'matrix' in node:return [[node['matrix'][j*4+i] for j in range(4)] for i in range(4)]
 x,y,z,w=node.get('rotation',[0,0,0,1]);sx,sy,sz=node.get('scale',[1,1,1]);t=node.get('translation',[0,0,0])
 return [[(1-2*y*y-2*z*z)*sx,(2*x*y-2*z*w)*sy,(2*x*z+2*y*w)*sz,t[0]],[(2*x*y+2*z*w)*sx,(1-2*x*x-2*z*z)*sy,(2*y*z-2*x*w)*sz,t[1]],[(2*x*z-2*y*w)*sx,(2*y*z+2*x*w)*sy,(1-2*x*x-2*y*y)*sz,t[2]],[0,0,0,1]]
def normal_matrix(m):
 a,b,c=m[0][:3];d,e,f=m[1][:3];g,h,i=m[2][:3];det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g)
 if abs(det)<1e-20:raise ValueError('Singular node transform')
 # Cofactor matrix / determinant = inverse transpose.
 return [[(e*i-f*h)/det,(f*g-d*i)/det,(d*h-e*g)/det],[(c*h-b*i)/det,(a*i-c*g)/det,(b*g-a*h)/det],[(b*f-c*e)/det,(c*d-a*f)/det,(a*e-b*d)/det]],det

def convert(source,destination):
 data=Path(source).read_bytes()
 magic,version,length=struct.unpack_from('<III',data)
 if magic!=0x46546c67 or version!=2 or length!=len(data):raise ValueError('Invalid GLB header')
 offset=12;doc=None;binary=None
 while offset<len(data):
  size,kind=struct.unpack_from('<II',data,offset);offset+=8;chunk=data[offset:offset+size];offset+=size
  if kind==0x4e4f534a:doc=json.loads(chunk)
  elif kind==0x004e4942:binary=chunk
 if doc is None or binary is None:raise ValueError('GLB requires JSON and embedded BIN chunks')
 if doc.get('animations') or doc.get('skins'):raise ValueError('Animated/skinned GLB is unsupported')
 def accessor(index):
  a=doc['accessors'][index]
  if 'sparse' in a or a.get('normalized'):raise ValueError('Sparse/normalized accessors unsupported')
  v=doc['bufferViews'][a['bufferView']]
  if v.get('buffer',0)!=0:raise ValueError('External buffers unsupported')
  kind={5126:'f',5125:'I',5123:'H',5121:'B'}.get(a['componentType']);count={'SCALAR':1,'VEC2':2,'VEC3':3}.get(a['type'])
  if not kind or not count:raise ValueError('Unsupported accessor')
  fmt='<'+kind*count;width=struct.calcsize(fmt);stride=v.get('byteStride',width);start=v.get('byteOffset',0)+a.get('byteOffset',0)
  if stride<width or start+(a['count']-1)*stride+width>len(binary):raise ValueError('Accessor outside buffer')
  return [struct.unpack_from(fmt,binary,start+k*stride) for k in range(a['count'])]
 atlas={};vertices=[];indices=[];parts=[];pivots=[[0,0,0,1] for _ in range(4)]
 def texture(info,kind):
  if not info:raise ValueError('Prepared GLB requires '+kind+' texture')
  if info.get('extensions'):raise ValueError('Texture transforms unsupported; bake them first')
  image=doc['textures'][info['index']]['source']
  if kind in atlas and atlas[kind]!=image:raise ValueError('All parts must share the baked '+kind+' atlas')
  atlas[kind]=image
  return info.get('texCoord',0)
 def walk(index,parent):
  node=doc['nodes'][index];matrix=matmul(parent,local(node))
  if 'mesh' in node:
   name=node.get('name','part_'+str(index));control=-1
   if name.startswith('B') and name.endswith('_push_btn'):control=int(name.split('_')[0][1:])-1
   for k,n in enumerate(CONTROLS):
    if name==n:control=8+k
   if control not in range(-1,18):raise ValueError('Invalid control number')
   nm,det=normal_matrix(matrix);start=len(indices);part_points=[]
   for prim in doc['meshes'][node['mesh']]['primitives']:
    if prim.get('mode',4)!=4 or prim.get('targets'):raise ValueError('Only static triangles supported')
    mat=doc['materials'][prim['material']]
    if mat.get('alphaMode','OPAQUE')!='OPAQUE':raise ValueError('Only opaque materials supported')
    pbr=mat.get('pbrMetallicRoughness',{});uv=texture(pbr.get('baseColorTexture'),'basecolor')
    if texture(mat.get('normalTexture'),'normal')!=uv or texture(pbr.get('metallicRoughnessTexture'),'orm')!=uv:raise ValueError('All textures on a part must use the same atlas UV')
    if mat['normalTexture'].get('scale',1)!=1:raise ValueError('Normal scale must be baked')
    attr=prim['attributes'];positions=accessor(attr['POSITION']);normals=accessor(attr['NORMAL']);uvs=accessor(attr['TEXCOORD_'+str(uv)])
    if not len(positions)==len(normals)==len(uvs):raise ValueError('Attribute count mismatch')
    base=pbr.get('baseColorFactor',[1,1,1,1])[:3];emission=mat.get('emissiveFactor',[0,0,0]);strength=mat.get('extensions',{}).get('KHR_materials_emissive_strength',{}).get('emissiveStrength',1);emission=[v*strength for v in emission]
    factors=[pbr.get('metallicFactor',1),pbr.get('roughnessFactor',1)];voffset=len(vertices)
    for pos,nor,tex in zip(positions,normals,uvs):
     p=[sum(matrix[i][k]*pos[k] for k in range(3))+matrix[i][3] for i in range(3)];n=[sum(nm[i][k]*nor[k] for k in range(3)) for i in range(3)];magnitude=math.sqrt(sum(v*v for v in n))
     if magnitude<1e-12:raise ValueError('Degenerate normal')
     row=p+[v/magnitude for v in n]+list(tex)+base+emission+factors+[float(control)]
     if not all(math.isfinite(v) for v in row):raise ValueError('Non-finite vertex')
     vertices.append(row);part_points.append(p)
    ix=[v[0] for v in accessor(prim['indices'])]
    if len(ix)%3 or any(v>=len(positions) for v in ix):raise ValueError('Invalid indices')
    if det<0:
     for k in range(0,len(ix),3):ix[k+1],ix[k+2]=ix[k+2],ix[k+1]
    indices.extend(v+voffset for v in ix)
   bounds=[[min(p[i] for p in part_points) for i in range(3)],[max(p[i] for p in part_points) for i in range(3)]]
   if 8<=control<12:
    # Approximate hinge: inner edge, middle height, front edge. Calibrate in Blender for precision.
    inner=min([bounds[0][0],bounds[1][0]],key=abs);pivots[control-8]=[inner,(bounds[0][1]+bounds[1][1])/2,bounds[1][2],1 if control in [8,10] else -1]
   parts.append({'name':name,'control':control,'index_start':start,'index_count':len(indices)-start,'bounds':bounds})
  for child in node.get('children',[]):walk(child,matrix)
 identity=[[1 if i==j else 0 for j in range(4)] for i in range(4)]
 for index in doc['scenes'][doc.get('scene',0)]['nodes']:walk(index,identity)
 if not vertices or len(vertices)>2_000_000 or len(indices)>6_000_000:raise ValueError('Asset exceeds runtime limits')
 width=max(v[0] for v in vertices)-min(v[0] for v in vertices)
 dest=Path(destination);dest.mkdir(parents=True,exist_ok=True)
 with (dest/'wheel.neo').open('wb') as f:
  f.write(struct.pack('<8sIIIf',b'NEOWHL2\0',2,len(vertices),len(indices),width))
  for p in pivots:f.write(struct.pack('<4f',*p))
  for v in vertices:f.write(struct.pack('<17f',*v))
  f.write(struct.pack('<'+str(len(indices))+'I',*indices))
 for kind,index in atlas.items():
  image=doc['images'][index]
  if image.get('mimeType')!='image/png' or 'bufferView' not in image:raise ValueError('Only embedded PNG supported')
  v=doc['bufferViews'][image['bufferView']];start=v.get('byteOffset',0);image_data=binary[start:start+v['byteLength']]
  if not image_data.startswith(b'\x89PNG\r\n\x1a\n'):raise ValueError('Invalid PNG')
  (dest/('wheel-'+kind+'.png')).write_bytes(image_data)
 report={'source':str(source),'vertex_count':len(vertices),'triangle_count':len(indices)//3,'width_m':width,'parts':parts,'paddle_pivots':pivots}
 (dest/'wheel-parts.json').write_text(json.dumps(report,indent=2))
 print('Converted',len(parts),'parts,',len(indices)//3,'triangles, width',round(width*1000,3),'mm')
 return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('glb');p.add_argument('output_directory');a=p.parse_args();convert(a.glb,a.output_directory)
