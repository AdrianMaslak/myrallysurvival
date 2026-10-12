"""Build original low-poly countryside assets in Blender, without touching vehicle art.
blender --background --factory-startup --python scripts/build_countryside_art.py
"""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import countryside_layout as layout
import bpy
import math
import random
import json
from collections import defaultdict

ART=layout.ART
EXPORT=ART/'Export'
EXPORT.mkdir(parents=True,exist_ok=True)
rs=layout.routes(); stats=layout.validate(rs); links=layout.accesses(rs)
roads=rs+links; index=layout.RoadIndex(roads)
rng=random.Random(7819)

# Linear colours shared by the Blender preview and Unreal material builder.
PALETTE={
 'Grass':(.16,.205,.085),'GrassLight':(.215,.25,.105),'GrassDark':(.10,.15,.068),
 'Wheat':(.36,.29,.115),'Earth':(.19,.125,.065),'Gravel':(.29,.26,.21),
 'Asphalt':(.065,.072,.078),'Concrete':(.36,.37,.34),'White':(.77,.76,.66),
 'Bark':(.12,.073,.038),'Pine':(.045,.11,.071),'PineLight':(.08,.16,.085),
 'Leaf':(.15,.23,.08),'LeafDark':(.08,.15,.052),'Roof':(.24,.073,.041),
 'RoofDark':(.09,.105,.105),'Plaster':(.66,.59,.43),'PlasterGreen':(.32,.40,.27),
 'PlasterRose':(.48,.30,.23),'Glass':(.055,.13,.16),'Wood':(.27,.15,.07),
 'Black':(.026,.03,.028),'Red':(.57,.055,.032),'Blue':(.045,.16,.3),'Yellow':(.78,.44,.06),
}
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system='METRIC'
bpy.context.scene.unit_settings.scale_length=1.
materials={}
for name,c in PALETTE.items():
    m=bpy.data.materials.new('M_Country_'+name);m.diffuse_color=(*c,1)
    m.use_nodes=True;m.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=(*c,1)
    m.node_tree.nodes.get('Principled BSDF').inputs['Roughness'].default_value=.88
    materials[name]=m


class Mesh:
    # collision: 'complex' uses the triangles (terrain, roads: keeps every bump), 'simple' gives each solid
    # obstacle part its own convex hull (walls, roofs, trunks), None has no collision.
    def __init__(self,name,group,collision='complex',surface=None,cull=0):
        self.name=name;self.group=group;self.collision=collision;self.surface=surface;self.cull=cull
        self.v=[];self.f=[];self.m=[];self.slots=[];self.hulls=[]

    def add(self,verts,faces,mat,hull=True):
        base=len(self.v);self.v.extend(verts)
        if mat not in self.slots:self.slots.append(mat)
        mi=self.slots.index(mat)
        for f in faces:self.f.append(tuple(base+i for i in f));self.m.append(mi)
        # Every primitive is convex, so it is its own hull; trim (window frames, doors) passes hull=False.
        if self.collision=='simple' and hull:self.hulls.append((list(verts),list(faces)))

    def box(self,center,size,mat,yaw=0,hull=True):
        sx,sy,sz=[s/2 for s in size];c=math.cos(yaw);s=math.sin(yaw)
        vs=[]
        for x,y,z in [(-sx,-sy,-sz),(sx,-sy,-sz),(sx,sy,-sz),(-sx,sy,-sz),
                      (-sx,-sy,sz),(sx,-sy,sz),(sx,sy,sz),(-sx,sy,sz)]:
            vs.append((center[0]+c*x-s*y,center[1]+s*x+c*y,center[2]+z))
        self.add(vs,[(0,3,2,1),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7),(4,5,6,7)],mat,hull)

    def cone(self,x,y,z,r1,r2,height,mat,n=7,rot=0):
        vs=[]
        for zz,rr in ((z,r1),(z+height,r2)):
            vs.extend((x+rr*math.cos(2*math.pi*i/n+rot),y+rr*math.sin(2*math.pi*i/n+rot),zz) for i in range(n))
        fs=[tuple(reversed(range(n))),tuple(range(n,2*n))]
        fs.extend((i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n))
        self.add(vs,fs,mat)

    def save(self):
        if not self.f:return None
        # Author in the game frame. FBX converts Blender Y to Unreal -Y.
        data=bpy.data.meshes.new(self.name)
        data.from_pydata([(x,-y,z) for x,y,z in self.v],[],[tuple(reversed(f)) for f in self.f])
        data.update()
        obj=bpy.data.objects.new(self.name,data);bpy.context.collection.objects.link(obj)
        # Unreal's FBX importer turns UCX_<mesh>_NN objects into convex collision elements of <mesh>.
        hull_objs=[]
        for i,(hv,hf) in enumerate(self.hulls):
            hd=bpy.data.meshes.new(f'UCX_{self.name}_{i:03}')
            hd.from_pydata([(x,-y,z) for x,y,z in hv],[],[tuple(reversed(f)) for f in hf]);hd.update()
            ho=bpy.data.objects.new(hd.name,hd);bpy.context.collection.objects.link(ho);hull_objs.append(ho)
        for name in self.slots:data.materials.append(materials[name])
        for p,mi in zip(data.polygons,self.m):p.material_index=mi
        # Planar UVs for future texture detailing, independent of the car UVs.
        uv=data.uv_layers.new(name='UVMap')
        for p in data.polygons:
            for li in p.loop_indices:
                v=data.vertices[data.loops[li].vertex_index].co;uv.data[li].uv=(v.x/8,v.y/8)
        bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
        for ho in hull_objs:ho.select_set(True)
        bpy.ops.export_scene.fbx(filepath=str(EXPORT/(self.name+'.fbx')),use_selection=True,object_types={'MESH'},
            axis_forward='-Y',axis_up='Z',apply_unit_scale=True,apply_scale_options='FBX_SCALE_ALL',
            bake_anim=False,use_mesh_modifiers=True,mesh_smooth_type='FACE',use_triangles=True,add_leaf_bones=False)
        return dict(name=self.name,file=self.name+'.fbx',materials=self.slots,group=self.group,
                    collision=self.collision,hulls=len(self.hulls),surface=self.surface,cull_m=self.cull,
                    bounds_m=[[min(v[k] for v in self.v) for k in range(3)],
                              [max(v[k] for v in self.v) for k in range(3)]],
                    triangles=sum(len(f)-2 for f in self.f))


meshes=[]
def mesh(*args,**kwargs):
    m=Mesh(*args,**kwargs);meshes.append(m);return m

# Terrain tiles share exactly the same boundary vertices. Each can be culled separately.
for tx in range(5):
    for ty in range(5):
        m=mesh(f'SM_Country_Terrain_{tx}_{ty}','01_Terrain',surface='Ground')
        x0=-850+tx*350;y0=-820+ty*294;nx=70;ny=59
        vv=[(x0+i*350/nx,y0+j*294/ny,index.ground(x0+i*350/nx,y0+j*294/ny))
            for j in range(ny+1) for i in range(nx+1)]
        # Group cells by colour for low draw-call field patches.
        groups=defaultdict(list)
        for j in range(ny):
            for i in range(nx):
                x=x0+(i+.5)*350/nx;y=y0+(j+.5)*294/ny
                value=math.sin(x/71)*math.cos(y/58)
                mat='GrassLight' if value>.4 else ('GrassDark' if value<-.45 else 'Grass')
                if y<-190 and index.nearest(x,y)[0]>13 and -400<x<560:
                    stripe=int((x+700)/90)+int((y+800)/115)*3
                    mat=['Wheat','GrassLight','Earth','Grass'][stripe%4]
                a=j*(nx+1)+i;groups[mat].extend([(a,a+1,a+nx+2),(a,a+nx+2,a+nx+1)])
        m.v=vv
        for mat,ff in groups.items():
            m.slots.append(mat);m.f.extend(ff);m.m.extend([len(m.slots)-1]*len(ff))

# Continuous ribbon roads: no overlapping cube slabs, and every seam shares a height.
for r in roads:
    pts=r['samples'];w=r['width']/2
    kinds={}
    shoulder=mesh('SM_Country_'+r['id']+'_Shoulder','02_Roads',surface='Ground')
    for i,(a,b) in enumerate(zip(pts,pts[1:])):
        surf=r['surface']
        if surf=='Mixed':surf='Gravel' if i<len(pts)*.56 else 'Asphalt'
        if surf not in kinds:kinds[surf]=mesh('SM_Country_'+r['id']+'_'+surf,'02_Roads',surface=surf)
        ta=layout.tangent(pts,i);tb=layout.tangent(pts,i+1)
        def offset(p,t,v,dz=0):return (p[0]-t[1]*v,p[1]+t[0]*v,p[2]+dz)
        kinds[surf].add([offset(a,ta,-w),offset(b,tb,-w),offset(b,tb,w),offset(a,ta,w)],[(0,1,2,3)],surf)
        for sign in (-1,1):
            vv=[offset(a,ta,sign*w,-.005),offset(b,tb,sign*w,-.005),
                offset(b,tb,sign*(w+2),-.08),offset(a,ta,sign*(w+2),-.08)]
            shoulder.add(vv,[(0,1,2,3) if sign==1 else (3,2,1,0)],'Gravel')

yard=mesh('SM_Country_ServiceApron','03_Service',surface='Gravel')
yard.box((0,0,-.16),(132,112,.32),'Gravel')
lines=mesh('SM_Country_RoadPaint','05_Details',collision=None,cull=750)
for r in rs:
    if r['surface']=='Gravel':continue
    pts=r['samples']
    for i in range(0,len(pts)-2,5):
        if r['surface']=='Mixed' and i<len(pts)*.56:continue
        a=pts[i];b=pts[i+1];dx,dy=b[0]-a[0],b[1]-a[1]
        length=math.hypot(dx,dy);nx=-dy/length*.05;ny=dx/length*.05
        lines.add([(a[0]-nx,a[1]-ny,a[2]+.02),(b[0]-nx,b[1]-ny,b[2]+.02),
                   (b[0]+nx,b[1]+ny,b[2]+.02),(a[0]+nx,a[1]+ny,a[2]+.02)],[(0,1,2,3)],'White')

buildings=mesh('SM_Country_Village','04_Buildings',collision='simple')
service=mesh('SM_Country_ServiceBuildings','03_Service',collision='simple')
details=mesh('SM_Country_YardDetails','03_Service',collision=None,cull=400)

def house(m,x,y,z,w,d,h,yaw,wall='Plaster',roof='Roof'):
    c,s=math.cos(yaw),math.sin(yaw)
    def world(a,b,zz):return (x+c*a-s*b,y+s*a+c*b,z+zz)
    m.box(world(0,0,h/2),(w,d,h),wall,yaw)
    # Pitched roof, with gable ends and generous eaves.
    v=[world(-w/2-.45,-d/2-.45,h),world(w/2+.45,-d/2-.45,h),
       world(w/2+.45,d/2+.45,h),world(-w/2-.45,d/2+.45,h),
       world(-w/2-.45,0,h+2.4),world(w/2+.45,0,h+2.4)]
    m.add(v,[(0,1,5,4),(4,5,2,3),(0,4,3),(1,2,5)],roof)
    # Windows on both long facades and a front door; geometry gives readable frames.
    for side in (-1,1):
        for xx in (-w*.29,w*.29):
            m.box(world(xx,side*(d/2+.035),2.25),(1.55,.09,1.6),'White',yaw,hull=False)
            m.box(world(xx,side*(d/2+.09),2.25),(1.29,.06,1.32),'Glass',yaw,hull=False)
            m.box(world(xx,side*(d/2+.13),2.25),(.07,.03,1.35),'White',yaw,hull=False)
    m.box(world(0,-d/2-.05,1.25),(1.05,.13,2.5),'Wood',yaw,hull=False)
    m.box(world(w*.26,d*.2,h+1.8),(.7,.8,2.7),'Plaster',yaw)

village_clear=[]
for j,i in enumerate(range(38,155,9)):
    p=rs[0]['samples'][i];t=layout.tangent(rs[0]['samples'],i);side=(-1 if j%2 else 1)
    dist=rng.uniform(20,29);x=p[0]-t[1]*dist*side;y=p[1]+t[0]*dist*side
    yaw=math.atan2(t[1],t[0]);z=index.ground(x,y)-.35
    house(buildings,x,y,z,rng.uniform(9,12),rng.uniform(7,9),rng.uniform(4.1,5.2),yaw,
          ['Plaster','PlasterGreen','PlasterRose'][j%3],['Roof','RoofDark'][j%2])
    village_clear.append((x,y,15))
    # Low garden fence on the roadside; centre gate remains open.
    for offset in (-7,7):
        fx=x+t[0]*offset+t[1]*side*6;fy=y+t[1]*offset-t[0]*side*6
        buildings.box((fx,fy,index.ground(fx,fy)+.5),(7,.14,1),'Wood',yaw)

house(service,-39,-36,-.2,27,15,5.4,0,'Plaster','RoofDark')
house(service,-39,39,-.2,17,11,4.5,0,'PlasterGreen','Roof')
# Freestanding open service canopy; collision supports stay outside the bays.
service.box((27,35,5.2),(32,20,.4),'RoofDark')
for x in (12,42):
    for y in (26,44):service.box((x,y,2.5),(.25,.25,5),'Red')
for x in (18,28,38):
    for y in (28,42):details.box((x,y,.02),(7,.13,.02),'White')
for i in range(8):
    details.cone(-24+i*1.3,-25,0,.43,.43,.8,'Blue',n=10)
for y in (-36,-32,-28):
    details.box((-53,y,.45),(1.6,1.1,.9),'Wood')

# Farm landmarks occupy safe areas beside the third stage.
farm=mesh('SM_Country_FarmBuildings','04_Buildings',collision='simple')
for x,y,w,d in ((220,-400,25,14),(-160,-335,19,12),(100,-420,14,9)):
    z=index.ground(x,y)-.3;house(farm,x,y,z,w,d,5.5,.2,'Wood','RoofDark');village_clear.append((x,y,max(w,d)))
    for dx in (-18,-23):farm.cone(x+dx,y,z,3,3,8,'Concrete',n=12)

# Forest comes in small spatial clusters for useful bounds/culling and modest actor counts.
trees=defaultdict(list)
for _ in range(5200):
    x=rng.uniform(-770,790);y=rng.uniform(-725,580)
    forest=(x<-90 and y>-180)
    if not forest and rng.random()>.13:continue
    if abs(x)<90 and abs(y)<90:continue
    if index.nearest(x,y)[0]<11.5:continue
    if any(math.hypot(x-a,y-b)<d+5 for a,b,d in village_clear):continue
    if y<-230 and not forest and rng.random()>.25:continue
    trees[(int((x+850)//180),int((y+820)//180))].append((x,y,index.ground(x,y),forest))
tree_count=0
for (cx,cy),items in sorted(trees.items()):
    # One hull per trunk, never one around the cluster: the gaps between trees stay drivable.
    trunks=mesh(f'SM_Country_Trunks_{cx}_{cy}','06_Forest',collision='simple',cull=1000)
    leaves=mesh(f'SM_Country_Foliage_{cx}_{cy}','06_Forest',collision=None,cull=1100)
    for x,y,z,forest in items:
        tree_count+=1;h=rng.uniform(9,17) if forest else rng.uniform(7,12);rad=rng.uniform(2.3,3.7)
        trunks.cone(x,y,z,.25,.12,h*.8,'Bark',n=6)
        if forest and rng.random()<.8:
            for k in range(3):leaves.cone(x,y,z+h*(.22+k*.2),rad*(1-k*.22),.05,h*.46,
                'Pine' if k%2 else 'PineLight',n=7,rot=rng.random())
        else:
            leaves.cone(x,y,z+h*.37,rad*.75,rad,h*.3,'LeafDark',n=8)
            leaves.cone(x,y,z+h*.67,rad,.05,h*.4,'Leaf',n=8)

markers=mesh('SM_Country_RouteFurniture','07_RouteMarkers',collision=None,cull=650)
texts=[];gates=[]
for r in rs:
    pts=r['samples'];w=r['width']/2
    for i in range(8,len(pts)-8,13):
        p=pts[i];t=layout.tangent(pts,i)
        for side in (-1,1):
            x=p[0]-t[1]*(w+1)*side;y=p[1]+t[0]*(w+1)*side;z=p[2]-.07
            markers.box((x,y,z+.55),(.14,.14,1.1),'White')
            markers.box((x,y,z+.9),(.17,.17,.18),'Red' if side<0 else 'Black')
    for i,title in ((0,'START'),(len(pts)-1,'FINISH')):
        p=pts[i];t=layout.tangent(pts,i);yaw=math.atan2(t[1],t[0]);right=(-t[1],t[0])
        for side in (-1,1):
            x=p[0]+right[0]*(w+1.6)*side;y=p[1]+right[1]*(w+1.6)*side
            markers.box((x,y,p[2]+2.8),(.18,.18,5.6),'White')
        markers.box((p[0],p[1],p[2]+5.25),(.2,r['width']+3.4,.8),'Red' if title=='START' else 'Black',yaw)
        texts.append(dict(text=r['id']+' '+title,position=[p[0]-t[0]*.15,p[1]-t[1]*.15,p[2]+5.05],
                          yaw=math.degrees(yaw)+180,size=.43,color='White'))
        gates.append(dict(stage=r['id'],kind=title,position=p,yaw=math.degrees(yaw)))
    # Boards sit beyond the shoulders and face approaching traffic.
    for i in range(50,len(pts)-25,65):
        p=pts[i];t=layout.tangent(pts,i);yaw=math.degrees(math.atan2(t[1],t[0]))+180
        x=p[0]+t[1]*(w+2.5);y=p[1]-t[0]*(w+2.5)
        markers.box((x,y,p[2]+1.3),(.14,.14,2.6),'Wood')
        markers.box((x,y,p[2]+2.2),(.12,2.3,.9),'Yellow',math.radians(yaw))
        texts.append(dict(text=r['id']+'  >',position=[x-t[0]*.09,y-t[1]*.09,p[2]+2.05],yaw=yaw,size=.34,color='Black'))
texts.extend([
 dict(text='LIPOVA\nSERVICE / DEPO',position=[-38,-28.42,3.2],yaw=90,size=.65,color='White'),
 dict(text='SS1  VILLAGE  >',position=[51,-11,1.8],yaw=180,size=.48,color='Yellow'),
 dict(text='<  SS2  PINE RIDGE',position=[-26,48,2.2],yaw=-90,size=.48,color='White'),
 dict(text='SS3  FIELDS  >',position=[12,-49,2.2],yaw=90,size=.48,color='White'),
])

parts=[]
for m in meshes:
    result=m.save()
    if result:parts.append(result)
layout.write_overview(rs,stats)
manifest=dict(map=layout.MAP,materials=PALETTE,parts=parts,stages=stats,gates=gates,texts=texts,
              routes=[dict(id=r['id'],width=r['width'],surface=r['surface'],samples=r['samples']) for r in roads],
              tree_count=tree_count,seed=7819,player_starts=[[3,-3,1.1],[3,3,1.1]],
              cameras=[dict(name='Service',position=[80,-108,49],target=[-2,6,1]),
                       dict(name='Village',position=[260,-185,54],target=[290,-84,1]),
                       dict(name='Forest',position=[-245,267,30],target=[-301,337,3]),
                       dict(name='Overview',position=[800,-1000,850],target=[-30,-60,0])])
(EXPORT/'countryside.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
(ART/'route-stats.json').write_text(json.dumps(stats,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(ART/'Countryside.blend'))
print(f'COUNTRYSIDE_ART_PASS: {len(parts)} meshes, {tree_count} trees, {sum(p["triangles"] for p in parts)} triangles')
