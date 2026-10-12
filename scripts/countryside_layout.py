"""Deterministic countryside layout. Metres, Unreal axes; no engine dependencies.

Owns only the new countryside map. Run with Python for layout and route validation.
"""
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "Art/Maps/Countryside"
MAP = "/Game/Maps/Countryside/L_Countryside"
BOUNDS = (-850, 900, -820, 650)
STAGES = [
    dict(id="SS1", name="Lipova Village", width=6.6, color="#e6ad57", surface="Asphalt",
         points=[(75,-32),(155,-45),(270,-100),(390,-105),(525,-60),(600,40),
                 (555,135),(450,190),(345,240),(240,180),(180,85),(85,35)]),
    dict(id="SS2", name="Pine Ridge", width=5.8, color="#81b797", surface="Gravel",
         points=[(-45,75),(-85,150),(-150,270),(-245,335),(-365,325),(-460,245),
                 (-505,130),(-480,15),(-560,-80),(-450,-160),(-310,-120),
                 (-210,-65),(-105,-30),(-65,30)]),
    dict(id="SS3", name="Fields of Hlinik", width=6.2, color="#85aee3", surface="Mixed",
         points=[(40,-75),(105,-170),(200,-255),(305,-350),(390,-415),(310,-505),
                 (170,-550),(30,-510),(-75,-440),(-180,-465),(-290,-400),
                 (-250,-285),(-130,-230),(-45,-145),(-25,-75)]),
]


def smooth(t):
    t = max(0., min(1., t))
    return t*t*(3-2*t)


def natural_height(x, y):
    def wave(a,b):
        return (12*math.sin(a/170)*math.sin(b/230) + 7*math.sin((a+b)/210)
                + 26*math.exp(-((a+340)**2+(b-220)**2)/(2*180**2))
                + 14*math.exp(-((a-350)**2+(b+410)**2)/(2*170**2)))
    return (wave(x,y)-wave(0,0))*smooth((math.hypot(x,y)-85)/125)


def spline(points, spacing=3.):
    """Cubic Hermite curve sampled at approximately uniform arc length."""
    dense = []
    for i in range(len(points)-1):
        p0,p1 = points[max(0,i-1)],points[i]
        p2,p3 = points[i+1],points[min(len(points)-1,i+2)]
        for j in range(80):
            t=j/80
            h00=2*t**3-3*t*t+1; h10=t**3-2*t*t+t
            h01=-2*t**3+3*t*t; h11=t**3-t*t
            dense.append(tuple(h00*p1[k]+h10*.8*(p2[k]-p0[k])/2+
                               h01*p2[k]+h11*.8*(p3[k]-p1[k])/2 for k in range(2)))
    dense.append(points[-1])
    out=[dense[0]]; carry=0.
    for a,b in zip(dense,dense[1:]):
        d=math.dist(a,b)
        while carry+d >= spacing:
            t=(spacing-carry)/d
            a=tuple(a[k]+(b[k]-a[k])*t for k in range(2))
            out.append(a); d=math.dist(a,b); carry=0.
        carry+=d
    if math.dist(out[-1],points[-1])>.01:
        out.append(points[-1])
    return [(x,y,natural_height(x,y)) for x,y in out]


def routes():
    out=[]
    for item in STAGES:
        r=dict(item); r['samples']=spline(item['points'])
        r['length_m']=sum(math.dist(a,b) for a,b in zip(r['samples'],r['samples'][1:]))
        out.append(r)
    return out


def accesses(rs):
    out=[]
    # All access ends are inside the service apron, away from buildings and spawn.
    anchors=[((48,-25),(48,25)),((-12,44),(-46,20)),((25,-40),(-20,-40))]
    for r,ends in zip(rs,anchors):
        for i,(p,anchor) in enumerate(zip((r['samples'][0],r['samples'][-1]),ends)):
            samples=spline([anchor,p[:2]])
            out.append(dict(id=r['id']+f'_Access{i}', samples=samples, width=7.0,
                            surface='Gravel', name='Service link'))
    return out


class RoadIndex:
    """Spatial buckets keep terrain and scenery checks independent of route length."""
    def __init__(self, road_list):
        self.buckets={}; self.cell=40.
        for r in road_list:
            for a,b in zip(r['samples'],r['samples'][1:]):
                seg=(a,b,r['width'])
                for ix in range(math.floor((min(a[0],b[0])-35)/self.cell),math.floor((max(a[0],b[0])+35)/self.cell)+1):
                    for iy in range(math.floor((min(a[1],b[1])-35)/self.cell),math.floor((max(a[1],b[1])+35)/self.cell)+1):
                        self.buckets.setdefault((ix,iy),[]).append(seg)

    def nearest(self,x,y):
        best=(float('inf'),0.,0.)
        for a,b,w in self.buckets.get((math.floor(x/self.cell),math.floor(y/self.cell)),[]):
            dx,dy=b[0]-a[0],b[1]-a[1]
            t=max(0.,min(1.,((x-a[0])*dx+(y-a[1])*dy)/(dx*dx+dy*dy)))
            d=math.hypot(x-a[0]-t*dx,y-a[1]-t*dy)
            if d<best[0]: best=(d,a[2]+t*(b[2]-a[2]),w)
        return best

    def ground(self,x,y):
        # A flat service apron joins smoothly to the surrounding landscape.
        service=max(abs(x)/68,abs(y)/58)
        if service<=1: return -.10
        h=natural_height(x,y)
        h=(-.10)*(1-smooth((service-1)/.45))+h*smooth((service-1)/.45)
        d,z,w=self.nearest(x,y)
        blend=1-smooth((d-w/2-2)/17)
        return h*(1-blend)+(z-.10)*blend


def tangent(samples,i):
    a=samples[max(0,i-1)]; b=samples[min(len(samples)-1,i+1)]
    d=math.hypot(b[0]-a[0],b[1]-a[1])
    return ((b[0]-a[0])/d,(b[1]-a[1])/d)


def validate(rs):
    stats=[]
    for r in rs:
        pts=r['samples']; grades=[]; radii=[]
        for a,b in zip(pts,pts[1:]):
            grades.append(abs(b[2]-a[2])/math.dist(a[:2],b[:2]))
        for a,b,c in zip(pts,pts[1:],pts[2:]):
            area2=abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))
            if area2>1e-6:
                radii.append(math.dist(a[:2],b[:2])*math.dist(b[:2],c[:2])*math.dist(c[:2],a[:2])/(2*area2))
        assert 1000 <= r['length_m'] <= 2000, (r['id'],r['length_m'])
        assert max(grades)<.16, (r['id'],'grade',max(grades))
        assert min(radii)>10, (r['id'],'radius',min(radii))
        # No non-adjacent self intersections in the centreline.
        def cross(a,b,c): return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
        for i,(a,b) in enumerate(zip(pts,pts[1:])):
            for j in range(i+2,len(pts)-1):
                c,d=pts[j:j+2]
                assert not (cross(a,b,c)*cross(a,b,d)<0 and cross(c,d,a)*cross(c,d,b)<0), (r['id'],'crossing')
        stats.append(dict(id=r['id'],name=r['name'],length_m=round(r['length_m'],1),
                          width_m=r['width'],max_grade_percent=round(max(grades)*100,1),
                          elevation_range_m=[round(min(p[2] for p in pts),1),round(max(p[2] for p in pts),1)],
                          min_radius_m=round(min(radii),1),surface=r['surface']))
    return stats


def write_overview(rs,stats):
    """Portable SVG route map, suitable for a browser or printed route sheet."""
    ART.mkdir(parents=True,exist_ok=True)
    def xy(p): return (p[0]*.63+510,410-p[1]*.63)
    def path(points): return ' '.join(f'{x:.1f},{y:.1f}' for x,y in map(xy,points))
    lines=['<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="980" viewBox="0 0 1200 980">',
           '<rect width="1200" height="980" fill="#17241f"/>',
           '<style>text{font-family:Segoe UI,Arial,sans-serif;fill:#ecede0}.small{font-size:15px}.label{font-size:19px;font-weight:600}</style>',
           '<text x="48" y="54" font-size="30" font-weight="700">LIPOVA / COUNTRYSIDE RALLY</text>',
           '<text x="48" y="84" class="small">Three stages · shared service yard · first playable environment</text>']
    for x in range(-700,801,100):
        a,b=xy((x,-650)),xy((x,430)); lines.append(f'<path d="M{a[0]},{a[1]} L{b[0]},{b[1]}" stroke="#24372d"/>')
    for y in range(-600,431,100):
        a,b=xy((-700,y)),xy((800,y)); lines.append(f'<path d="M{a[0]},{a[1]} L{b[0]},{b[1]}" stroke="#24372d"/>')
    for r in accesses(rs): lines.append(f'<polyline points="{path(r["samples"])}" fill="none" stroke="#879185" stroke-width="4"/>')
    for r in rs:
        lines.append(f'<polyline points="{path(r["samples"])}" fill="none" stroke="{r["color"]}" stroke-width="5" stroke-linejoin="round"/>')
        for i,letter in ((0,'S'),(-1,'F')):
            x,y=xy(r['samples'][i]); lines.append(f'<circle cx="{x}" cy="{y}" r="10" fill="{r["color"]}"/><text x="{x}" y="{y+4}" text-anchor="middle" style="fill:#17241f;font-size:12px;font-weight:bold">{letter}</text>')
        for i in (len(r['samples'])//3,2*len(r['samples'])//3):
            x,y=xy(r['samples'][i]); dx,dy=tangent(r['samples'],i); angle=math.degrees(math.atan2(-dy,dx))
            lines.append(f'<path d="M-8,-5 L3,0 L-8,5" transform="translate({x},{y}) rotate({angle})" fill="none" stroke="#fff" stroke-width="3"/>')
    x,y=xy((0,0)); lines.append(f'<rect x="{x-25}" y="{y-22}" width="50" height="44" rx="5" fill="#eee7c9"/><text x="{x}" y="{y+4}" text-anchor="middle" style="fill:#17241f;font-size:11px;font-weight:bold">SERVICE</text>')
    for r,pos in zip(rs,[(375,290),(-480,390),(50,-610)]):
        x,y=xy(pos);lines.append(f'<text x="{x}" y="{y}" class="label" text-anchor="middle">{r["id"]} / {r["name"]}</text>')
    lines.append('<path d="M1090,175 L1090,125 l-7,14 m7,-14 l7,14" fill="none" stroke="#eee7c9" stroke-width="2"/><text x="1090" y="112" text-anchor="middle" class="small">+Y</text>')
    for i,s in enumerate(stats):
        x=48+i*388; y=868
        lines.extend([f'<rect x="{x}" y="{y-32}" width="365" height="109" rx="7" fill="#23382e"/>',
                      f'<text x="{x+16}" y="{y}" class="label">{s["id"]} / {s["length_m"]/1000:.2f} km</text>',
                      f'<text x="{x+16}" y="{y+26}" class="small">{s["surface"]} · {s["width_m"]} m road</text>',
                      f'<text x="{x+16}" y="{y+50}" class="small">Max grade {s["max_grade_percent"]}% · min bend {s["min_radius_m"]} m</text>'])
    lines.append('</svg>');(ART/'Route_Overview.svg').write_text('\n'.join(lines),encoding='utf-8')


if __name__=='__main__':
    rs=routes(); stats=validate(rs); write_overview(rs,stats)
    (ART/'route-stats.json').write_text(json.dumps(stats,indent=2),encoding='utf-8')
    print(json.dumps(stats,indent=2))
