// SPDX-License-Identifier: GPL-3.0-or-later
R"hlsl(
ByteAddressBuffer src : register(t0);
ByteAddressBuffer second : register(t1);
RWByteAddressBuffer dst : register(u0);
cbuffer Params : register(b0) { uint n, w, h, ow, oh, channels, pitch, format; };
[numthreads(256,1,1)] void rgba_to_planar(uint3 tid : SV_DispatchThreadID) {
    uint i=tid.x+tid.y*16776960;
    if (i >= n) return;
    uint pixels = w*h, c = i/pixels, p = i%pixels;
    uint offset = (p/w)*pitch + (p%w)*(format==2?16:4);
    float v;
    if (format==2) v = asfloat(src.Load(offset+c*4));
    else { uint shift = (format==1 ? (c==0?2:(c==2?0:1)) : c)*8;
           v = ((src.Load(offset)>>shift)&255)/255.0; }
    dst.Store(i*4,asuint(v));
}
[numthreads(256,1,1)] void planar_to_rgba(uint3 tid : SV_DispatchThreadID) {
    uint i=tid.x+tid.y*16776960;
    if (i>=n) return;
    uint c=i%4,p=i/4;
    dst.Store(i*4,c==3?second.Load(p*4):src.Load((c*w*h+p)*4));
}
[numthreads(256,1,1)] void resize_area(uint3 tid : SV_DispatchThreadID) {
    uint i=tid.x+tid.y*16776960;
    if(i>=n) return;
    uint c=i/(ow*oh), y=i/ow%oh, x=i%ow;
    uint x0=x*w/ow,x1=((x+1)*w+ow-1)/ow,y0=y*h/oh,y1=((y+1)*h+oh-1)/oh;
    float v=0;
    for(uint yy=y0;yy<y1;++yy) for(uint xx=x0;xx<x1;++xx) v+=asfloat(src.Load(((c*h+yy)*w+xx)*4));
    dst.Store(i*4,asuint(v/((y1-y0)*(x1-x0))));
}
[numthreads(256,1,1)] void validate(uint3 tid : SV_DispatchThreadID) {
    uint i=tid.x+tid.y*16776960;
    if(i>=n) return;
    float v=asfloat(src.Load(i*4));
    if(!isfinite(v)||(w!=0&&(v<0||v>1))) { uint ignored; dst.InterlockedOr(0,1,ignored); }
}
[numthreads(256,1,1)] void unpack_atlas(uint3 tid : SV_DispatchThreadID) {
    uint i=tid.x+tid.y*16776960;
    if(i>=n) return;
    uint p=i%2704,c=i/2704%11,t=i/(2704*11);
    uint x=t%16*52+p%52,y=c/4*(w/16*52)+t/16*52+p/52;
    float v=asfloat(src.Load(y*pitch+x*16+c%4*4));
    if (!isfinite(v)||v<0||v>1) { dst.Store(i*4,0x7fc00000); return; }
    uint bits=asuint(v);
    if (bits>=0x38800000) bits=(bits+0xfff+((bits>>13)&1))&0xffffe000;
    else bits=asuint(round(v*16777216.0)/16777216.0);
    dst.Store(i*4,bits);
}
[numthreads(256,1,1)] void pack_centres(uint3 tid : SV_DispatchThreadID) {
    uint i=tid.x+tid.y*16776960;
    if(i>=n) return;
    uint c=i%4,x=i/4%512,y=i/(4*512),t=y/32*16+x/32,p=(y%32+10)*52+x%32+10;
    dst.Store(i*4,c==3?second.Load((t*2704+p)*4):src.Load(((t*3+c)*2704+p)*4));
}
)hlsl"
