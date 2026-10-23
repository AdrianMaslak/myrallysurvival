"""Blender: original low-poly props and a 16-bit Landscape heightmap."""
import sys
import json
import math
from pathlib import Path
import bpy
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parent))
import highlands_layout as layout

data=layout.create()
out=layout.ART
out.mkdir(parents=True,exist_ok=True)
(out/'route.json').write_text(json.dumps(data,separators=(',',':')))
buckets={}
for road in data['stages']+data['links']:
    for p in road['samples']:
        buckets.setdefault((int(p[0]//64),int(p[1]//64)),[]).append(p)
size=data['landscape']['size']
axis=np.arange(size)*8.-800
x,y=np.meshgrid(axis,axis)
h=110+85*np.sin(x/950)*np.cos(y/1100)+55*np.sin((x+y)/650)+30*np.cos(x/320)
for iy in range(0,size,8):
    for ix in range(0,size,8):
        xx=x[iy:iy+8,ix:ix+8]; yy=y[iy:iy+8,ix:ix+8]
        bx,by=int(xx[0,0]//64),int(yy[0,0]//64)
        near=[p for a in range(bx-5,bx+6) for b in range(by-5,by+6) for p in buckets.get((a,b),[])]
        if not near: continue
        p=np.array(near)
        d=(xx.reshape(-1,1)-p[:,0])**2+(yy.reshape(-1,1)-p[:,1])**2
        ids=d.argmin(axis=1)
        distance=np.sqrt(d[np.arange(len(ids)),ids]).reshape(xx.shape)
        road_z=p[ids,2].reshape(xx.shape)-.65
        t=np.clip((distance-16)/240,0,1)
        t=t*t*(3-2*t)
        h[iy:iy+8,ix:ix+8]=road_z*(1-t)+h[iy:iy+8,ix:ix+8]*t
for px,py,pz in data['services']+[[15,0,0]]:
    distance=np.maximum(np.abs(x-px),np.abs(y-py))
    t=np.clip((distance-38)/30,0,1);t=t*t*(3-2*t)
    h=(pz-.65)*(1-t)+h*t
encoded=np.rint((h-200)*64+32768)
assert encoded.min()>0 and encoded.max()<65535
encoded.astype('<u2').tofile(out/'Highlands.r16')

bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system='METRIC'
colors={'Bark':(.10,.055,.025),'Pine':(.045,.115,.065),'PineLight':(.09,.17,.07),
        'Rock':(.30,.32,.29),'Wood':(.27,.14,.055),'White':(.78,.76,.65),'Red':(.55,.045,.025)}
mats={}
for name,col in colors.items():
    m=bpy.data.materials.new(name); m.diffuse_color=(*col,1); mats[name]=m


def save(name,objects,collision):
    bpy.ops.object.select_all(action='DESELECT')
    for ob in objects: ob.select_set(True)
    bpy.context.view_layer.objects.active=objects[0]
    bpy.ops.object.join(); ob=bpy.context.object; ob.name=name
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    if collision:
        hull=ob.copy(); hull.data=ob.data.copy(); bpy.context.collection.objects.link(hull)
        hull.name='UCX_'+name+'_00'; hull.select_set(True)
        bpy.context.view_layer.objects.active=hull
        bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.mesh.convex_hull(); bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.export_scene.fbx(filepath=str(out/(name+'.fbx')),use_selection=True,object_types={'MESH'},
        axis_forward='-Y',axis_up='Z',apply_unit_scale=True,apply_scale_options='FBX_SCALE_ALL',
        bake_anim=False,mesh_smooth_type='FACE',use_triangles=True)


def cone(radius1,radius2,depth,z,material,vertices=7):
    bpy.ops.mesh.primitive_cone_add(vertices=vertices,radius1=radius1,radius2=radius2,depth=depth,location=(0,0,z))
    ob=bpy.context.object; ob.data.materials.append(mats[material]); return ob

tree=[cone(.24,.15,5,2.5,'Bark')]
for z,r in [(3,2.3),(4.6,1.9),(6,1.35)]: tree.append(cone(r,.06,3.5,z,'Pine' if z!=4.6 else 'PineLight'))
save('SM_HighlandPine',tree,False)
bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1,radius=.40)
rock=bpy.context.object; rock.scale=(1,.8,.7); rock.data.materials.append(mats['Rock'])
save('SM_LooseRock',[rock],True)
log=cone(.16,.19,2.4,0,'Bark',8); log.rotation_euler[1]=math.pi/2
save('SM_LooseLog',[log],True)
bpy.ops.wm.save_as_mainfile(filepath=str(out/'Highlands.blend'))
(out/'art.json').write_text(json.dumps({'colors':colors,'meshes':['SM_HighlandPine','SM_LooseRock','SM_LooseLog']},indent=2))
print('HIGHLANDS_ART_PASS')
