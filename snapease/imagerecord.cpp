/*
    SnapEase
    imagerecord.cpp -- image thumbnail/button/info/etc entry implementation
    Copyright (C) 2009-2010  Cockos Incorporated

    PathSync is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    PathSync is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with PathSync; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "main.h"
#include <math.h>

#include "../WDL/lice/lice.h"
#include "../WDL/lice/lice_text.h"
#include "../WDL/wingui/virtwnd-controls.h"

#ifdef ENABLE_FUN_TRANSFORM_TEST
#include "../WDL/plush2/plush.h"
#endif


#include "imagerecord.h"


#include "resource.h"

#define TRANSFORM_PT_RADIUS 3.0

enum
{

  BUTTONID_BASE=1000,
  
  BUTTONID_FULLSCREEN=BUTTONID_BASE, // upper left
  BUTTONID_ROTCCW,
  BUTTONID_ROTCW,
  BUTTONID_CROP,
  BUTTONID_BW,

  BUTTONID_REMOVE, // goes on upper right

  BUTTONID_END
};

static WDL_VirtualIconButton_SkinConfig *GetButtonIcon(int idx, char state=0);

static int GetNearestVWndToPoint(WDL_VWnd *par, int x, int y)
{
  RECT parr;
  par->GetPosition(&parr);
  if (x<parr.left||y<parr.top || x>=parr.right || y>=parr.bottom) return -1;

  int n=par->GetNumChildren(),a;
  int nearest1=-1, nearest1_xdiff=1<<20;
  int nearest2=-1, nearest2_diff=1<<30;

  for(a=0;a<n;a++)
  {
    WDL_VWnd *vw = par->EnumChildren(a);
    if (vw)
    {
      RECT r;
      vw->GetPosition(&r);
      int ydiff=0;
      if (y < r.top) ydiff=r.top-y;
      else if (y>r.bottom) ydiff=y-r.bottom;
      
      if (!ydiff)
      {
        int xdiff;
        if (x < r.left) xdiff=r.left-x;
        else if (x>r.right)xdiff=x-r.right;
        else return a;

        if (xdiff < nearest1_xdiff) { nearest1 = a; nearest1_xdiff=xdiff; }
      }
      else
      {
        int xdiff=0;
        if (x < r.left) xdiff=r.left-x;
        else if (x>r.right)xdiff=x-r.right;

        int diff = ydiff*ydiff + xdiff*xdiff;
        if (diff < nearest2_diff) 
        { 
          nearest2 = a; 
          nearest2_diff=diff;
        }
      }
    }
  }   
  if (nearest1>=0) return nearest1;
  return nearest2;
}

ImageRecord::ImageRecord(const char *fn)
{
  memset(&m_lastlbl_rect,0,sizeof(m_lastlbl_rect));
  memset(&m_last_drawrect,0,sizeof(m_last_drawrect));
  memset(&m_last_crop_drawrect,0,sizeof(m_last_crop_drawrect));
  memset(&m_crop_capmode_lastpos,0,sizeof(m_crop_capmode_lastpos));
  memset(&m_croprect,0,sizeof(m_croprect));

  m_is_fs=false;
  m_fullimage_rendercached=0;
  m_fullimage_rendercached_valid=0;
  m_edit_mode=EDIT_MODE_NONE;
  m_fullimage=0;
  m_want_fullimage=0;
  m_bw=false;
  m_rot=0;
  m_state=IR_STATE_NEEDLOAD;
  m_preview_image=NULL;
  m_srcimage_w=m_srcimage_h=0;
  m_fn.Set(fn);
  SetDefaultTitle();

  // add our button children
  int x;
  for(x=BUTTONID_BASE;x<BUTTONID_END;x++)
  {
    WDL_VirtualIconButton *b = new WDL_VirtualIconButton;
    b->SetID(x);
    AddChild(b);
  }

  UpdateButtonStates();
}

void ImageRecord::UpdateButtonStates()
{
  int x;
  for(x=BUTTONID_BASE;x<BUTTONID_END;x++)
  {
    WDL_VirtualIconButton *b = (WDL_VirtualIconButton*)GetChildByID(x);
    if (b)
    {
      char st=0;
      switch (x)
      {
        case BUTTONID_BW: st = m_bw; break;
        case BUTTONID_CROP: st = m_edit_mode==EDIT_MODE_CROP; break;
        case BUTTONID_FULLSCREEN: st = m_is_fs; break;
      }
      b->SetIcon(GetButtonIcon(x,st));
    }
  }
}

ImageRecord::~ImageRecord()
{
  EditImageLabelEnd();

  delete m_preview_image;
  delete m_fullimage;
  delete m_fullimage_rendercached;
  m_transform.Empty(true);
}

ImageRecord *ImageRecord ::Duplicate()
{
  ImageRecord  *rec = new ImageRecord(m_fn.Get());
  rec->m_outname.Set(m_outname.Get());
  if (m_state==IR_STATE_LOADED && m_preview_image)
  {
    rec->m_state=m_state;   
    LICE_Copy(rec->m_preview_image = new LICE_MemBitmap,m_preview_image);
  }
  rec->m_srcimage_w=m_srcimage_w;
  rec->m_srcimage_h=m_srcimage_h;
  rec->m_bw=m_bw;
  rec->m_rot=m_rot;
  rec->m_edit_mode=m_edit_mode;
  rec->m_croprect=m_croprect;

  rec->UpdateButtonStates();
  return rec;

}

void ImageRecord::SetIsFullscreen(bool isFS)
{
  if (isFS != m_is_fs)
  {
    m_is_fs = isFS;
    WDL_VirtualIconButton *c = (WDL_VirtualIconButton *)GetChildByID(BUTTONID_FULLSCREEN);
    if (c) c->SetIcon(GetButtonIcon(BUTTONID_FULLSCREEN,!!m_is_fs));

    c = (WDL_VirtualIconButton *)GetChildByID(BUTTONID_REMOVE);
    if (c) c->SetVisible(!m_is_fs);
  }
}


void ImageRecord::SetDefaultTitle()
{
  {
    const char *p = m_fn.Get();
    while (*p) p++;
    while (p >= m_fn.Get() && *p != '\\' && *p != '/') p--;
    m_outname.Set(++p);
  }
  {
    char *p = m_outname.Get();
    while (*p) p++;
    while (p >= m_outname.Get() && *p != '.') p--;
    if (p > m_outname.Get()) *p=0;
  }
}



LICE_CachedFont g_imagerecord_font;


#define BUTTON_SIZE 16

LICE_IBitmap *LoadThemeElement(int idx, const char *name)
{
#ifdef _WIN32
  LICE_IBitmap *bm = LICE_LoadPNGFromResource(g_hInst,idx,NULL);
#else
  LICE_IBitmap *bm =  LICE_LoadPNGFromNamedResource(name,NULL);

#endif
  return bm;

#if 0
  if (!bm) return 0;

  LICE_MemBitmap *bm2 = new LICE_MemBitmap(bm->getWidth()+4*3,bm->getHeight()+4);

  /* oops need to do this by x3
  LICE_Clear(bm2,0);
  LICE_DrawRect(bm2,0,0,bm2->getWidth()-1,bm2->getHeight()-1,LICE_RGBA(96,96,96,96),1.0f,LICE_BLIT_MODE_COPY);
  LICE_Blit(bm2,bm,2,2,NULL,1.0f,LICE_BLIT_MODE_COPY);
  LICE_FillRect(bm2,1,1,bm2->getWidth()-2,bm2->getHeight()-2,LICE_RGBA(64,64,64,255),0.25f,LICE_BLIT_MODE_COPY);
  */

  delete bm;

  return bm2;
#endif
}

static WDL_VirtualIconButton_SkinConfig *GetButtonIcon(int idx, char state)
{
  static WDL_VirtualIconButton_SkinConfig img_[BUTTONID_END-BUTTONID_BASE][2];
  static const char *names_[BUTTONID_END-BUTTONID_BASE][2];
  static int resids_[BUTTONID_END-BUTTONID_BASE][2];
  static bool init;
  if (!init)
  {
#define ASSIGN(x,st,name,resid) { names_[(x) - BUTTONID_BASE][st] = (name); resids_[(x) - BUTTONID_BASE][st]=(resid); }
    ASSIGN(BUTTONID_FULLSCREEN,0,"full_off.png",IDR_FULLOFF);
    ASSIGN(BUTTONID_FULLSCREEN,1,"full_on.png",IDR_FULLON);

    ASSIGN(BUTTONID_BW,0,"bw_off.png",IDR_BWOFF);
    ASSIGN(BUTTONID_BW,1,"bw_on.png",IDR_BWON);

    ASSIGN(BUTTONID_CROP,0,"crop_off.png",IDR_CROPOFF);
    ASSIGN(BUTTONID_CROP,1,"crop_on.png",IDR_CROPON);
    

    ASSIGN(BUTTONID_ROTCW,0,"rot_right.png",IDR_ROTR);
    ASSIGN(BUTTONID_ROTCCW,0,"rot_left.png",IDR_ROTL);
    ASSIGN(BUTTONID_REMOVE,0,"remove.png",IDR_REMOVE);
#undef ASSIGN
    init=true;
  }
  if (idx< BUTTONID_BASE || idx >= BUTTONID_END) return NULL;

  WDL_VirtualIconButton_SkinConfig *img = &img_[idx-BUTTONID_BASE][!!state];
  if (!img->image)
  {
    if (!names_[idx-BUTTONID_BASE][!!state] || !resids_[idx-BUTTONID_BASE][!!state]) return 0;

    img->image = LoadThemeElement(resids_[idx-BUTTONID_BASE][!!state],names_[idx-BUTTONID_BASE][!!state]);
    if (img->image) WDL_VirtualIconButton_PreprocessSkinConfig(img);
  }

  return img->image ? img : NULL;
}




INT_PTR ImageRecord::SendCommand(int command, INT_PTR parm1, INT_PTR parm2, WDL_VWnd *src)
{
  if (command == WM_COMMAND && src)
  {
    switch (src->GetID())
    {
      case BUTTONID_CROP:
        if (m_edit_mode==EDIT_MODE_CROP) m_edit_mode=EDIT_MODE_NONE;
        else m_edit_mode=EDIT_MODE_CROP;

        m_fullimage_rendercached_valid=false;
        ((WDL_VirtualIconButton *)src)->SetIcon(GetButtonIcon(src->GetID(),m_edit_mode==EDIT_MODE_CROP));
        RequestRedraw(NULL);
        SetImageListIsDirty();
      break;

      case BUTTONID_BW:
        m_bw=!m_bw;
        m_fullimage_rendercached_valid=false;
        ((WDL_VirtualIconButton *)src)->SetIcon(GetButtonIcon(src->GetID(),!!m_bw));
        RequestRedraw(NULL);
        SetImageListIsDirty();
      break;

      case BUTTONID_ROTCCW:
      case BUTTONID_ROTCW:

        m_rot= (m_rot+ (src->GetID() == BUTTONID_ROTCCW ? -1 : 1 ))&3;
        m_fullimage_rendercached_valid=false;

        RequestRedraw(NULL);

        SetImageListIsDirty();
      break;
      case BUTTONID_FULLSCREEN:
        {
          if (!RemoveFullItemView())
          {
            OpenFullItemView(this);
          }
        }
      break;
      case BUTTONID_REMOVE:
        {
          WDL_VWnd *par = GetParent();
          if (par)
          {
            if (!RemoveFullItemView())
            {
              g_images_mutex.Enter();
              g_images.Delete(g_images.Find(this));
              g_images_mutex.Leave();

              par->RemoveChild(this,true);
              // do nothing after this, "this" not valid anymore!
              UpdateMainWindowWithSizeChanged();

              SetImageListIsDirty();
            }
          }
        }

      break;
    }   
  }
  // dont allow anything upstream
  return 0; // WDL_VWnd::SendCommand(command,parm1,parm2,src);
}

void ImageRecord::SetPosition(const RECT *r)
{
  if ((r->right-r->left) != (m_position.right-m_position.left) ||
      (r->bottom-r->top) != (m_position.bottom-m_position.top))
  {
    // reposition images on size change
    int x;
    int xpos = 6;
    int toppos = 2;
    for (x = BUTTONID_BASE; x < BUTTONID_END; x ++)
    {
      WDL_VWnd *b = GetChildByID(x);
      if (b)
      {
        if (x==BUTTONID_REMOVE) 
        {
          RECT tr={r->right-r->left - 4 - BUTTON_SIZE, 2, r->right-r->left - 4 , toppos+BUTTON_SIZE};
          b->SetPosition(&tr);
        }
        else
        {
          if (xpos>6 && xpos+BUTTON_SIZE >= r->right-r->left - 4 - BUTTON_SIZE-4)
          {
            xpos = 2;
            toppos += BUTTON_SIZE+2;
          }
          RECT tr={xpos, toppos, xpos+BUTTON_SIZE, toppos+BUTTON_SIZE};
          b->SetPosition(&tr);
          if (x == BUTTONID_FULLSCREEN||x==BUTTONID_BW) xpos += 8;
          xpos += BUTTON_SIZE+2;
        }
      }
    }
  }

  WDL_VWnd::SetPosition(r);
}

static void makeBWFunc(LICE_pixel *p, void *parm)
{
  LICE_pixel pix = *p;
  int v = (LICE_GETR(pix) + LICE_GETG(pix) + LICE_GETB(pix))/3;
  *p = LICE_RGBA(v,v,v,255);
}



bool ImageRecord::SetCropRectFromScreen(int w, int h, const RECT *cr)
{
  if (w<1) w=1;
  if (h<1) h=1;

  int sw=m_srcimage_w;
  int sh=m_srcimage_h;
  RECT tr;

  switch (m_rot & 3)
  {
    case 0:
      tr.left = (cr->left * (double)sw) / w + 0.5;
      tr.right = (cr->right * (double)sw) / w + 0.5;
      tr.top = (cr->top * (double)sh) / h + 0.5;
      tr.bottom = (cr->bottom * (double)sh) /h + 0.5;
    break;
    case 1: // 90cw
      tr.left = (cr->top * (double)sw) / h + 0.5;
      tr.right = (cr->bottom * (double)sw) / h + 0.5;
      tr.top = sh - (cr->right * (double)sh) / w + 0.5;
      tr.bottom = sh - (cr->left * (double)sh) / w + 0.5;
    break;
    case 2:
      tr.left = (sw - (cr->right * (double)sw) / w) + 0.5;
      tr.right = (sw - (cr->left * (double)sw) / w) + 0.5;
      tr.top = (sh - (cr->bottom * (double)sh) / h) + 0.5;
      tr.bottom = (sh -  (cr->top * (double)sh) /h) + 0.5;
    break;
    case 3: // 90ccw
      tr.left = sw - (cr->bottom * (double)sw) / h + 0.5;
      tr.right = sw - (cr->top * (double)sw) / h + 0.5;
      tr.top = (cr->left * (double)sh) / w + 0.5;
      tr.bottom = (cr->right * (double)sh) / w + 0.5;
    break;
  }

  if (tr.left<0) tr.left=0; else if (tr.left > sw) tr.left = sw;
  if (tr.right<0) tr.right=0; else if (tr.right > sw) tr.right = sw;
  if (tr.top<0) tr.top=0; else if (tr.top > sh) tr.top = sh;
  if (tr.bottom<0) tr.bottom=0; else if (tr.bottom > sh) tr.bottom = sh;

  if (memcmp(&tr,&m_croprect,sizeof(RECT)))
  {
    m_croprect = tr;
    return true;
  }
  return false;
}


void ImageRecord::GetCropRectForScreen(int w, int h, RECT *cr)
{
  RECT inrect = m_croprect;
  if (inrect.left >= inrect.right)
  {
    inrect.right = m_srcimage_w;
    if (inrect.left >= inrect.right) inrect.left=0;
  }
  if (inrect.top >= inrect.bottom)
  {
    inrect.bottom =m_srcimage_h;
    if (inrect.top >= inrect.bottom) inrect.top=0;
  }

  // rotate
  int sw=max(m_srcimage_w,1);
  int sh=max(m_srcimage_h,1);
  switch  (m_rot&3)
  {
    case 0: 
      cr->left = (inrect.left * (double)w) / sw + 0.5;
      cr->top = (inrect.top * (double)h) / sh + 0.5;
      cr->right = (inrect.right * (double)w) / sw + 0.5;
      cr->bottom = (inrect.bottom * (double)h) / sh + 0.5;      
    break;
    case 1: // 90cw
      cr->left = w - (inrect.bottom * (double)w) / sh + 0.5;
      cr->right = w - (inrect.top * (double)w) / sh + 0.5;
      cr->top = (inrect.left * (double)h) / sw + 0.5;
      cr->bottom = (inrect.right * (double)h) / sw + 0.5;
    break;
    case 2: // 180
      cr->left = (w - (inrect.right * (double)w) / sw) + 0.5;
      cr->top = (h - (inrect.bottom * (double)h) / sh) + 0.5;
      cr->right = (w - (inrect.left * (double)w) / sw) + 0.5;
      cr->bottom = (h - (inrect.top * (double)h) / sh) + 0.5;      
    break;
    case 3: // 90ccw
      cr->left = (inrect.top * (double)w) / sh + 0.5;
      cr->right = (inrect.bottom * (double)w) / sh + 0.5;
      cr->top = h - (inrect.right * (double)h) / sw + 0.5;
      cr->bottom = h - (inrect.left * (double)h) / sw + 0.5;
    break;

  }

//  if (cr->left>cr->right) { int a=cr->left; cr->left=cr->right; cr->right=a; }
  //if (cr->top>cr->bottom) { int a=cr->top; cr->top=cr->bottom; cr->bottom=a; }

  // crop 
  if (cr->left<0) cr->left=0; else if (cr->left > w) cr->left = w;
  if (cr->right<0) cr->right=0; else if (cr->right > w) cr->right = w;
  if (cr->top<0) cr->top=0; else if (cr->top > h) cr->top = h;
  if (cr->bottom<0) cr->bottom=0; else if (cr->bottom > h) cr->bottom = h;
}


enum
{
  LOCAL_CAP_CROP=-1000,
  LOCAL_CAP_DRAGIMAGE,
  LOCAL_CAP_TRANSFORM,

};


bool ImageRecord::GetToolTipString(int xpos, int ypos, char *bufOut, int bufOutSz)
{
  if (WDL_VWnd::GetToolTipString(xpos,ypos,bufOut,bufOutSz)) return true;

  WDL_VWnd *v = VirtWndFromPoint(xpos,ypos,0);
  if (v)
  {
    int idx = v->GetID();
    switch (idx)
    {
      case BUTTONID_FULLSCREEN:
        lstrcpyn(bufOut,m_is_fs ? "Leave fullscreen" : "View image in fullscreen",bufOutSz);
      return true;
      case BUTTONID_ROTCCW:
        lstrcpyn(bufOut,"Rotate image counter-clockwise",bufOutSz);
      return true;
      case BUTTONID_ROTCW:
        lstrcpyn(bufOut,"Rotate image clockwise",bufOutSz);
      return true;
      case BUTTONID_CROP:
        {
          WDL_String s;
          s.Set(m_edit_mode==EDIT_MODE_CROP ? "Leave crop mode" : "Crop");
          char buf[512];
          GetSizeInfoString(buf,sizeof(buf));
          s.Append(" [image: ");
          s.Append(buf);
          s.Append("]");
          lstrcpyn(bufOut,s.Get(),bufOutSz);
        }
      return true;
      case BUTTONID_BW:
        lstrcpyn(bufOut,m_bw ? "Remove black & white" : "Set black & white",bufOutSz);
      return true;
      case BUTTONID_REMOVE:
        lstrcpyn(bufOut,"Remove image from list",bufOutSz);
      return true;
    }
  }
  else
  {
    POINT p ={xpos,ypos};
    if (PtInRect(&m_lastlbl_rect,p))
    {
      WDL_String tmp;
      char buf[512];
      GetSizeInfoString(buf,sizeof(buf));
      tmp.SetFormatted(1024,"Image #%d/%d [%s], source filename:",g_images.Find(this)+1,g_images.GetSize(),buf);
      tmp.Append(m_fn.Get());
      lstrcpyn(bufOut,tmp.Get(),bufOutSz);
      return true;
    }
  }

  return false;
}

int ImageRecord::UpdateCursor(int xpos, int ypos)
{
  int r = WDL_VWnd::UpdateCursor(xpos,ypos);

  if (r) return r; // pass on child requests

  if (VirtWndFromPoint(xpos,ypos,0)) return -1; // force default cursor for any children


  if (m_edit_mode==EDIT_MODE_CROP)
  {
    int cm=0,f=0;
    if (ypos >= m_last_crop_drawrect.top-3 && ypos <= m_last_crop_drawrect.bottom+3)
    {
      if (xpos >= m_last_crop_drawrect.left-3 && xpos <= m_last_crop_drawrect.left+3) cm|=1;
      else if (xpos >= m_last_crop_drawrect.right-3 && xpos <= m_last_crop_drawrect.right+3) cm|=4;
      else f|=1;
    }
    if (xpos >= m_last_crop_drawrect.left-3 && xpos <= m_last_crop_drawrect.right+3)
    {
      if (ypos >= m_last_crop_drawrect.top-3 && ypos <= m_last_crop_drawrect.top+3) cm|=2;
      else if (ypos >= m_last_crop_drawrect.bottom-3 && ypos <= m_last_crop_drawrect.bottom+3) cm|=8;
      else f|=2;
    }

    if (f==3) 
    {
      SetCursor(LoadCursor(NULL,MAKEINTRESOURCE(IDC_SIZEALL)));
      return 1;
    }
    if (cm&5) // left or right
    {
      INT_PTR idx=(INT_PTR)IDC_SIZEWE;

      if (cm&10) idx = (INT_PTR) ( ((cm&1)^!!(cm&2)) ? IDC_SIZENESW : IDC_SIZENWSE);

      SetCursor(LoadCursor(NULL,MAKEINTRESOURCE(idx)));
      return 1;
    }   
    if (cm&10)
    {
      SetCursor(LoadCursor(NULL,MAKEINTRESOURCE(IDC_SIZENS)));
      return 1;
    }
  }

  return -1;
}

bool ImageRecord::OnMouseDblClick(int xpos, int ypos)
{
  if (WDL_VWnd::OnMouseDblClick(xpos,ypos)) return true;

  if (!RemoveFullItemView())
  {
    OpenFullItemView(this);
  }

  return true;
}

int ImageRecord::OnMouseDown(int xpos, int ypos)
{
  int a = WDL_VWnd::OnMouseDown(xpos,ypos);
  if (!a)
  {
    POINT p ={xpos,ypos};
    if (PtInRect(&m_lastlbl_rect,p))
    {
      EditImageLabel(this);
      
      return 1;
    }
  }
  if (!a)
  {
#ifdef ENABLE_FUN_TRANSFORM_TEST
    if (m_edit_mode==EDIT_MODE_TRANSFORM)
    {
      m_captureidx= LOCAL_CAP_TRANSFORM;

      int capcnt = 0;
      double wscale = (m_last_drawrect.right-m_last_drawrect.left) / (double) m_srcimage_w;
      double hscale = (m_last_drawrect.bottom-m_last_drawrect.top) / (double) m_srcimage_h;
      int i;

      for(i=0;i<m_transform.GetSize();i++)
      {
        TransformTriangle *t = m_transform.Get(i);

        t->cap[0]=t->cap[1]=t->cap[2]=false;

        int a;
        for(a=0;a<3;a++)
        {
          int x = m_last_drawrect.left + (int) (t->x[a]*wscale + 0.5) - xpos;
          int y = m_last_drawrect.top + (int) (t->y[a]*hscale + 0.5) - ypos;
          if (x * x + y * y <= (int)(TRANSFORM_PT_RADIUS*TRANSFORM_PT_RADIUS + 0.5))
          {
            t->cap[a]=true;
            t->cap_offs[a].x = x;
            t->cap_offs[a].y = y;
            capcnt++;
            break;
          }
        }

      }

      if (!capcnt)
      {
        int orig_sz = m_transform.GetSize();
        double xfx = (xpos - m_last_drawrect.left) / wscale;
        double xfy = (ypos - m_last_drawrect.top) / hscale;
        for(i=0;i<orig_sz;i++)
        {
          TransformTriangle *t = m_transform.Get(i);
          // todo check edges first
          double v0x = t->x[2] - t->x[0], v0y = t->y[2] - t->y[0];
          double v1x = t->x[1] - t->x[0], v1y = t->y[1] - t->y[0];
          double v2x = xfx - t->x[0], v2y = xfy - t->y[0];
          double dot00 = v0x*v0x + v0y*v0y;
          double dot01 = v0x*v1x + v0y*v1y;
          double dot02 = v0x*v2x + v0y*v2y;
          double dot11 = v1x*v1x + v1y*v1y;
          double dot12 = v1x*v2x + v1y*v2y;

          double iv = 1.0/(dot00 * dot11 - dot01 * dot01);
          double uu = (dot11 * dot02 - dot01 * dot12) * iv;
          double vv = (dot00 * dot12 - dot01 * dot02) * iv;

          if (uu >= -0.01 && vv >= -0.01 && (uu + vv <= 1.1))
          {
            double calcu = vv * t->u[1] + uu*t->u[2] + (1.0 - uu - vv)*t->u[0];
            double calcv = vv * t->v[1] + uu*t->v[2] + (1.0 - uu - vv)*t->v[0];


            if (uu < 0.01) // todo better factor
            {
              // on edge between [0] and [1]
              TransformTriangle *nt = new TransformTriangle(t->x[0],t->y[0],t->x[2],t->y[2], xfx, xfy);
              nt->cap[2]=true;
              nt->u[0]=t->u[0];
              nt->v[0]=t->v[0];
              nt->u[1]=t->u[2];
              nt->v[1]=t->v[2];
              nt->u[2]=calcu;
              nt->v[2]=calcv;
              m_transform.Add(nt);

              t->x[0]=xfx;
              t->y[0]=xfy;
              t->u[0]=calcu;
              t->v[0]=calcv;
              t->cap[0]=true;
              t->cap_offs[0].x=t->cap_offs[0].y=0;
            }
            else if (vv < 0.01)
            {
              // on edge between [0] and [2]
              TransformTriangle *nt = new TransformTriangle(t->x[0],t->y[0],t->x[1],t->y[1], xfx, xfy);
              nt->cap[2]=true;
              nt->u[0]=t->u[0];
              nt->v[0]=t->v[0];
              nt->u[1]=t->u[1];
              nt->v[1]=t->v[1];
              nt->u[2]=calcu;
              nt->v[2]=calcv;
              m_transform.Add(nt);

              t->x[0]=xfx;
              t->y[0]=xfy;
              t->u[0]=calcu;
              t->v[0]=calcv;
              t->cap[0]=true;
              t->cap_offs[0].x=t->cap_offs[0].y=0;
            }
            else if (uu+vv >= 0.98)
            {
              // on edge between [1] and [2]
              TransformTriangle *nt = new TransformTriangle(t->x[0],t->y[0],t->x[1],t->y[1], xfx, xfy);
              nt->cap[2]=true;
              nt->u[0]=t->u[0];
              nt->v[0]=t->v[0];
              nt->u[1]=t->u[1];
              nt->v[1]=t->v[1];
              nt->u[2]=calcu;
              nt->v[2]=calcv;
              m_transform.Add(nt);

              t->x[1]=xfx;
              t->y[1]=xfy;
              t->u[1]=calcu;
              t->v[1]=calcv;
              t->cap[1]=true;
              t->cap_offs[1].x=t->cap_offs[1].y=0;
            }
            else
            {
              TransformTriangle *nt = new TransformTriangle(t->x[0],t->y[0],t->x[1],t->y[1], xfx, xfy);
              nt->cap[2]=true;
              nt->u[0]=t->u[0];
              nt->v[0]=t->v[0];
              nt->u[1]=t->u[1];
              nt->v[1]=t->v[1];
              nt->u[2]=calcu;
              nt->v[2]=calcv;
              m_transform.Add(nt);

              nt = new TransformTriangle(t->x[0],t->y[0],t->x[2],t->y[2], xfx, xfy);
              nt->cap[2]=true;
              nt->u[0]=t->u[0];
              nt->v[0]=t->v[0];
              nt->u[1]=t->u[2];
              nt->v[1]=t->v[2];
              nt->u[2]=calcu;
              nt->v[2]=calcv;
              m_transform.Add(nt);

              t->x[0]=xfx;
              t->y[0]=xfy;
              t->u[0]=calcu;
              t->v[0]=calcv;
              t->cap[0]=true;
              t->cap_offs[0].x=t->cap_offs[0].y=0;
            }

            m_fullimage_rendercached=false;
          }

        }
      }
      RequestRedraw(NULL);

      return 1;
    }
    else 
#endif
      if (m_edit_mode==EDIT_MODE_CROP)
    {
      m_capture_state = 0;
      int f=0;
      if (ypos >= m_last_crop_drawrect.top-3 && ypos <= m_last_crop_drawrect.bottom+3)
      {
        if (xpos >= m_last_crop_drawrect.left-3 && xpos <= m_last_crop_drawrect.left+3) m_capture_state|=1;
        else if (xpos >= m_last_crop_drawrect.right-3 && xpos <= m_last_crop_drawrect.right+3) m_capture_state|=4;
        else f|=1;
      }
      if (xpos >= m_last_crop_drawrect.left-3 && xpos <= m_last_crop_drawrect.right+3)
      {
        if (ypos >= m_last_crop_drawrect.top-3 && ypos <= m_last_crop_drawrect.top+3) m_capture_state|=2;
        else if (ypos >= m_last_crop_drawrect.bottom-3 && ypos <= m_last_crop_drawrect.bottom+3) m_capture_state|=8;
        else f|=2;
      }

      if (f==3)  m_capture_state=0xf;
      m_crop_capmode_lastpos.x = xpos - ((m_capture_state & 1) ? m_last_crop_drawrect.left : m_last_crop_drawrect.right);
      m_crop_capmode_lastpos.y = ypos - ((m_capture_state & 2) ? m_last_crop_drawrect.top : m_last_crop_drawrect.bottom);
    
      if (m_capture_state)
      {
        m_captureidx = LOCAL_CAP_CROP;
        return 1;
      }
      // hit test
    }

    if (!m_is_fs)
    {
      m_capture_state = -1;
      m_captureidx = LOCAL_CAP_DRAGIMAGE;
      return 1;
    }
  }
  return a;
}

int ImageRecord::UserIsDraggingImageToPosition(int *typeOut)
{
  if (!m_is_fs && m_captureidx==LOCAL_CAP_DRAGIMAGE)
  {
    *typeOut = (GetAsyncKeyState(VK_CONTROL)&0x8000) ? 2 : 1;
    return m_capture_state;
  }
  *typeOut=0;
  return -1;
}

void ImageRecord::OnMouseMove(int xpos, int ypos)
{
  switch (m_captureidx)
  {
    case LOCAL_CAP_TRANSFORM:
#ifdef ENABLE_FUN_TRANSFORM_TEST
      {
        double uwscale = m_srcimage_w / (double) (m_last_drawrect.right-m_last_drawrect.left);
        double uhscale = m_srcimage_h / (double) (m_last_drawrect.bottom-m_last_drawrect.top);
        int cnt=0,i;
        for(i=0;i<m_transform.GetSize();i++)
        {
          TransformTriangle *t = m_transform.Get(i);
          int a;
          for(a=0;a<3;a++)
          {
            if (t->cap[a])
            {
              t->x[a] = uwscale * (xpos - m_last_drawrect.left - t->cap_offs[a].x);
              t->y[a] = uhscale * (ypos - m_last_drawrect.top - t->cap_offs[a].y);
              cnt++;
            }
          }
        }
        if (cnt)
        {
          m_fullimage_rendercached=false;
          RequestRedraw(NULL);
        }
      }
#endif
    return;
    case LOCAL_CAP_DRAGIMAGE:
      if (!m_is_fs && GetParent())
      {
        int newidx= GetNearestVWndToPoint(GetParent(),xpos+m_position.left,ypos+m_position.top);
        if (newidx>=0)
        {
          WDL_VWnd *vw = GetParent()->EnumChildren(newidx);
          if (vw)
          {
            if (vw==this) newidx=-1;
            else
            {
              RECT r;
              vw->GetPosition(&r);
              if (xpos+m_position.left >= r.left+(r.right-r.left)/2) newidx++;
            }
          }
        }

        if (newidx!=m_capture_state)
        {
          m_capture_state=newidx;
          GetParent()->RequestRedraw(NULL);
        }        
      }
    return;
    case LOCAL_CAP_CROP:
      if (m_capture_state)
      {
        RECT r;
        GetCropRectForScreen(m_last_drawrect.right-m_last_drawrect.left,m_last_drawrect.bottom-m_last_drawrect.top,&r);


        if (m_capture_state==0xf) // move all
        {
          int dx = (xpos - m_crop_capmode_lastpos.x) - r.left  - m_last_drawrect.left;
          if (r.left+dx<0) dx=-r.left;
          else if (r.right+dx>m_last_drawrect.right-m_last_drawrect.left) dx = m_last_drawrect.right-m_last_drawrect.left - r.right;

          int dy = (ypos - m_crop_capmode_lastpos.y) - r.top - m_last_drawrect.top;
          if (r.top+dy<0) dy=-r.top;
          else if (r.bottom+dy>m_last_drawrect.bottom-m_last_drawrect.top) dy = m_last_drawrect.bottom-m_last_drawrect.top- r.bottom;

          r.left += dx;
          r.top += dy;
          r.right += dx;
          r.bottom += dy;
        }
        else
        {
          if (m_capture_state&1) r.left = xpos - m_crop_capmode_lastpos.x - m_last_drawrect.left;
          if (m_capture_state&4) r.right = xpos - m_crop_capmode_lastpos.x - m_last_drawrect.left;
          if (m_capture_state&2) r.top = ypos - m_crop_capmode_lastpos.y - m_last_drawrect.top;
          if (m_capture_state&8) r.bottom = ypos - m_crop_capmode_lastpos.y - m_last_drawrect.top;
        }
        if (SetCropRectFromScreen(m_last_drawrect.right-m_last_drawrect.left,m_last_drawrect.bottom-m_last_drawrect.top,&r))
        {
          RequestRedraw(NULL);
          SetImageListIsDirty();
        }
      }
    return;
  }

  WDL_VWnd::OnMouseMove(xpos,ypos);
}

void ImageRecord::OnMouseUp(int xpos, int ypos)
{
  switch (m_captureidx)
  {
    case LOCAL_CAP_TRANSFORM: 
      m_captureidx= -1;
    return;
    case LOCAL_CAP_CROP:

      // todo: notify that edit finished?
      m_captureidx= -1;

    return; 
    case LOCAL_CAP_DRAGIMAGE:

      // reorder with parent, then invalidate parent
      m_captureidx= -1;
      m_capture_state=-1;
      if (!m_is_fs && GetParent()) 
      {
        int newidx= GetNearestVWndToPoint(GetParent(),xpos+m_position.left,ypos+m_position.top);
        if (newidx>=0)
        {
          WDL_VWnd *vw = GetParent()->EnumChildren(newidx);
          if (vw)
          {
            if (vw==this) newidx=-1;
            else
            { 
              RECT r;
              vw->GetPosition(&r);
              if (xpos+m_position.left >= r.left+(r.right-r.left)/2) newidx++;
            }
          }
        }

        bool didmove=false;
        if (newidx>=0)
        {
          // reorder self to newidx
          int idx=g_images.Find(this);
          bool doCopy = !!(GetAsyncKeyState(VK_CONTROL)&0x8000);
          if (newidx!=idx||doCopy)
          {
            ImageRecord *newrec=NULL;
            if (doCopy)
            {
              newrec = Duplicate();
              AddImageRec(newrec,newidx);
            }
            else
            {
              if (newidx>idx) newidx--;
              WDL_VWnd *par=GetParent();
              par->RemoveChild(this,false);
              par->AddChild(this,newidx);
              g_images_mutex.Enter();
              g_images.Delete(idx);
              g_images.Insert(newidx,this);
              g_images_mutex.Leave();
            }

            SetImageListIsDirty(true);

            UpdateMainWindowWithSizeChanged();
            EnsureImageRecVisible(newrec ? newrec : this);
            didmove=true;
          }
        }
        if (!didmove) GetParent()->RequestRedraw(NULL);
      }

    return;
  }
  WDL_VWnd::OnMouseUp(xpos,ypos);
}


bool ImageRecord::ProcessImageToBitmap(LICE_IBitmap *srcimage, LICE_IBitmap *destimage, int max_w, int max_h)
{
  if (!srcimage || !destimage) return false;

  LICE_SubBitmap subbm(srcimage, m_croprect.left,m_croprect.top,m_croprect.right-m_croprect.left,m_croprect.bottom-m_croprect.top); 
  if (m_croprect.right-m_croprect.left>0&&m_croprect.bottom-m_croprect.top>0)
  {
    if (subbm.getWidth()<1||subbm.getHeight()<1) return false; // this would catch if it exceeds the bounds of the image, perhaps?
    srcimage=&subbm;
  }

  int srcw=srcimage->getWidth();
  int srch=srcimage->getHeight();


  int rot = m_rot&3;
  if (rot&1)
  {
    int a= srcw;
    srcw=srch;
    srch=a;
  }

  double out_w = srcw;
  double out_h = srch;
  if (max_w && out_w > max_w) 
  {
    out_h = (out_h * max_w) / out_w;
    out_w = max_w;
  }
  if (max_h && out_h > max_h)
  {
    out_w = (out_w * max_h) / out_h;
    out_h = max_h;
  }

  int w = (int) (out_w+0.5);
  int h = (int) (out_h+0.5);

  if (w<1 || h < 1) return false;

  destimage->resize(w,h);

  // todo: better filtering perhaps? this is used for final image generation ,bleh

  if (!rot) LICE_ScaledBlit(destimage,srcimage,0,0,w,h,0,0,srcimage->getWidth(),srcimage->getHeight(),1.0f,LICE_BLIT_MODE_COPY|LICE_BLIT_FILTER_BILINEAR);
  else
  {

    double dsdx=0, dsdy=0,dtdx=0,dtdy=0;

    int sx = rot != 1 ? srcimage->getWidth() - 1 : 0;
    int sy = rot != 3 ? srcimage->getHeight() - 1 : 0;

    if (rot!=2)
    {
      dtdx = srcimage->getHeight() / (double) w;
      dsdy = srcimage->getWidth() / (double) h;
      if (rot==1) dtdx=-dtdx;
      else dsdy=-dsdy;
    }
    else // flip
    {
      dsdx=-srcimage->getWidth() / (double) w;
      dtdy=-srcimage->getHeight() / (double) h;
    }

    LICE_DeltaBlit(destimage,srcimage,0,0,w,h,
              sx,sy, // start x,y
          srcimage->getWidth(),srcimage->getHeight(),
            dsdx, dtdx,
            dsdy, dtdy,
            0,0,false,1.0f,LICE_BLIT_MODE_COPY|LICE_BLIT_FILTER_BILINEAR);

  }
  if (m_bw)
  {
    LICE_ProcessRect(destimage,0,0,w,h,makeBWFunc,NULL);
  }


  return true;
}


void ImageRecord::OnPaint(LICE_IBitmap *drawbm, int origin_x, int origin_y, RECT *cliprect)
{
  if (!g_imagerecord_font.GetHFont())
  {
    bool doOutLine = 
#ifdef _WIN32
    true
#else
    false
#endif
    ;
    LOGFONT lf = 
    {
        14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,doOutLine ? NONANTIALIASED_QUALITY : DEFAULT_QUALITY,DEFAULT_PITCH,
	    #ifdef _WIN32
        "MS Shell Dlg"
	    #else
	    "Arial"
	    #endif
    };

    g_imagerecord_font.SetFromHFont(CreateFontIndirect(&lf),LICE_FONT_FLAG_OWNS_HFONT|(doOutLine?LICE_FONT_FLAG_FX_OUTLINE:0));
  }

  RECT r;
  GetPosition(&r);
  r.left += origin_x;
  r.top += origin_y;
  r.right += origin_x;
  r.bottom += origin_y;
  LICE_DrawRect(drawbm,r.left,r.top,r.right-r.left,r.bottom-r.top,LICE_RGBA(32,32,32,32),1.0f,LICE_BLIT_MODE_COPY);

  g_imagerecord_font.SetTextColor(LICE_RGBA(255,255,255,0));
  g_imagerecord_font.SetEffectColor(LICE_RGBA(0,0,0,0));

  bool usedFullImage=(m_fullimage &&!m_want_fullimage) ;

//  WDL_MutexLock tmp(usedFullImage ? &g_images_mutex : NULL);
  LICE_IBitmap *srcimage = usedFullImage ? m_fullimage : m_preview_image;
  if (srcimage)
  {
    int cropw=0,croph=0,cropl=0,cropt=0;

    if (m_edit_mode!=EDIT_MODE_CROP && m_croprect.right > m_croprect.left && m_croprect.bottom > m_croprect.top)
    {
      // scale coordinates to match this image

      cropl = (m_croprect.left * srcimage->getWidth()) / max(m_srcimage_w,1);
      cropt = (m_croprect.top * srcimage->getHeight()) / max(m_srcimage_h,1);
      cropw = ((m_croprect.right * srcimage->getWidth()) / max(m_srcimage_w,1)) - cropl;
      croph = ((m_croprect.bottom * srcimage->getHeight()) / max(m_srcimage_h,1)) - cropt;

      if (cropw < 1) cropw=1;
      if (croph < 1) croph=1;

    }

    LICE_SubBitmap subbm(srcimage, cropl,cropt,cropw,croph); //unused if not cropping
    if (cropw>0&&croph>0)srcimage=&subbm;

    int srcw=srcimage->getWidth();
    int srch=srcimage->getHeight();


    int rot = m_rot&3;
    if (rot&1)
    {
      int a= srcw;
      srcw=srch;
      srch=a;
    }

    int destw = r.right-r.left-4;
    int desth = r.bottom-r.top-4;
    // center image

    int w=destw,h=desth;
    if (srcw*desth > destw*srch) // src image wider than tall
    {
      h = (destw * srch) / max(srcw,1);
    }
    else
    {
      w = (desth * srcw) / max(srch,1);
    }

    int yoffs = (desth-h)/2 + r.top + 2;
    int xoffs = (destw-w)/2 + r.left + 2;

    m_last_drawrect.left=xoffs - r.left;
    m_last_drawrect.top = yoffs - r.top;
    m_last_drawrect.right = m_last_drawrect.left + w;
    m_last_drawrect.bottom = m_last_drawrect.top + h;


#ifdef ENABLE_FUN_TRANSFORM_TEST
    if (m_edit_mode!=EDIT_MODE_TRANSFORM)
    {
      m_edit_mode=EDIT_MODE_TRANSFORM;
      m_transform.Add(new TransformTriangle(0,0,      m_srcimage_w,0,0,m_srcimage_h));
      m_transform.Add(new TransformTriangle(m_srcimage_w,m_srcimage_h,m_srcimage_w,0,0,m_srcimage_h));
    }
#endif

    // todo: cache scaled/rotated version in global cache if srcimage == m_fullimage?

    if (usedFullImage &&
        m_fullimage_rendercached && m_fullimage_rendercached_valid && 
        m_fullimage_rendercached->getWidth() == w && m_fullimage_rendercached->getHeight() == h)
    {
      LICE_Blit(drawbm,m_fullimage_rendercached,xoffs,yoffs,0,0,w,h,1.0f,LICE_BLIT_MODE_COPY);
    }
    else
    {
      m_fullimage_rendercached_valid=false;

#ifdef ENABLE_FUN_TRANSFORM_TEST
      if (m_transform.GetSize())
      {
        pl_Cam cam;
        cam.WantZBuffer=false;
        cam.Begin(drawbm);

        double xsc = w / (double)m_srcimage_w;
        double ysc = h / (double)m_srcimage_h;
        double wsc = 1.0 / (double)m_srcimage_w;
        double hsc = 1.0 / (double)m_srcimage_h;
        pl_Mat mat;
        mat.SolidOpacity=0.0;
        mat.Texture = srcimage;
        mat.TexCombineMode=LICE_BLIT_MODE_COPY|LICE_BLIT_FILTER_BILINEAR;

        pl_Face tmp;
        memset(&tmp,0,sizeof(tmp));
        tmp.Material = &mat;

        int x;
        for(x=0;x<m_transform.GetSize();x++)
        {
          TransformTriangle *t = m_transform.Get(x);
          int a;
          for(a=0;a<3;a++)
          {
            tmp.MappingU[0][a] = t->u[a] * wsc;
            tmp.MappingV[0][a] = t->v[a] * hsc;
            tmp.Scrx[a]=xoffs + t->x[a]*xsc;
            tmp.Scry[a]=yoffs + t->y[a]*ysc;
            tmp.Scrz[a] = 100.0;
            if (tmp.Scrx[a]<0||tmp.Scrx[a]>=drawbm->getWidth()) break;
            if (tmp.Scry[a]<0||tmp.Scry[a]>=drawbm->getHeight()) break;
          }
          if (a>=3)
            cam.PutFace(&tmp);

        }

        cam.End();
        // draw as triangles
      }
      else 
#endif  
      if (!rot)
        LICE_ScaledBlit(drawbm,srcimage,xoffs,yoffs,w,h,0,0,srcimage->getWidth(),srcimage->getHeight(),1.0f,LICE_BLIT_MODE_COPY|LICE_BLIT_FILTER_BILINEAR);
      else
      {

        double dsdx=0, dsdy=0,dtdx=0,dtdy=0;

        int sx = rot != 1 ? srcimage->getWidth() - 1 : 0;
        int sy = rot != 3 ? srcimage->getHeight() - 1 : 0;

        if (rot!=2)
        {
          dtdx = srcimage->getHeight() / (double) w;
          dsdy = srcimage->getWidth() / (double) h;
          if (rot==1) dtdx=-dtdx;
          else dsdy=-dsdy;
        }
        else // flip
        {
          dsdx=-srcimage->getWidth() / (double) w;
          dtdy=-srcimage->getHeight() / (double) h;
        }

        LICE_DeltaBlit(drawbm,srcimage,xoffs,yoffs,w,h,
                  sx,sy, // start x,y
              srcimage->getWidth(),srcimage->getHeight(),
                dsdx, dtdx,
                dsdy, dtdy,
                0,0,false,1.0f,LICE_BLIT_MODE_COPY|LICE_BLIT_FILTER_BILINEAR);

      }
      if (m_bw)
      {
        LICE_ProcessRect(drawbm,xoffs,yoffs,w,h,makeBWFunc,NULL);
        //LICE_FillRect(drawbm,xoffs,yoffs,w,h,LICE_RGBA(128,0,128,255),1.0f,LICE_BLIT_MODE_HSVADJ);
      }

      if (usedFullImage)
      {
        if (!m_fullimage_rendercached) m_fullimage_rendercached=new LICE_MemBitmap;
        m_fullimage_rendercached->resize(w,h);
        LICE_Blit(m_fullimage_rendercached,drawbm,0,0,xoffs,yoffs,w,h,1.0f,LICE_BLIT_MODE_COPY);
        m_fullimage_rendercached_valid=true;
      }
      else
      {
        delete m_fullimage_rendercached;
        m_fullimage_rendercached=0;
      }

    }


#ifdef ENABLE_FUN_TRANSFORM_TEST
    if (m_edit_mode==EDIT_MODE_TRANSFORM)
    {
      int x;
      LICE_pixel col = LICE_RGBA(255,255,255,255);
      float al = 0.5;
      int mode=LICE_BLIT_MODE_COPY;
      bool aa=true;
      double wscale = w / (double) m_srcimage_w;
      double hscale = h / (double) m_srcimage_h;
      double cr=TRANSFORM_PT_RADIUS;

      if (m_edit_mode!=EDIT_MODE_CROP) for(x=0;x<m_transform.GetSize();x++)
      {
        TransformTriangle *t = m_transform.Get(x);
        int x1 = (int) (xoffs + t->x[0] * wscale + 0.5);
        int x2 = (int) (xoffs + t->x[1] * wscale + 0.5);
        int x3 = (int) (xoffs + t->x[2] * wscale + 0.5);
        int y1 = (int) (yoffs + t->y[0] * hscale + 0.5);
        int y2 = (int) (yoffs + t->y[1] * hscale + 0.5);
        int y3 = (int) (yoffs + t->y[2] * hscale + 0.5);

        if (t->cap[0] && m_captureidx == LOCAL_CAP_TRANSFORM)
          LICE_FillCircle(drawbm,x1,y1,cr,col,al,mode,aa);
        else 
          LICE_Circle(drawbm,x1,y1,cr,col,al,mode,aa);
        
        if (t->cap[1] && m_captureidx == LOCAL_CAP_TRANSFORM)
          LICE_FillCircle(drawbm,x2,y2,cr,col,al,mode,aa);
        else 
          LICE_Circle(drawbm,x2,y2,cr,col,al,mode,aa);

        if (t->cap[2] && m_captureidx == LOCAL_CAP_TRANSFORM)
          LICE_FillCircle(drawbm,x3,y3,cr,col,al,mode,aa);
        else 
          LICE_Circle(drawbm,x3,y3,cr,col,al,mode,aa);

        LICE_Line(drawbm,x1,y1,x2,y2,col,al,mode,aa);
        LICE_Line(drawbm,x3,y3,x2,y2,col,al,mode,aa);
        LICE_Line(drawbm,x1,y1,x3,y3,col,al,mode,aa);
      }
    }
    else 
#endif
      if (m_edit_mode==EDIT_MODE_CROP)
    {

      RECT cr;
      GetCropRectForScreen(w,h,&cr);

      cr.left += xoffs;
      cr.right += xoffs;
      cr.bottom += yoffs;
      cr.top += yoffs;

      m_last_crop_drawrect = cr;
      m_last_crop_drawrect.top -= r.top;
      m_last_crop_drawrect.bottom -= r.top;
      m_last_crop_drawrect.left -= r.left;
      m_last_crop_drawrect.right -= r.left;

      float al = 0.85f;
      LICE_pixel col = 0;
      if (cr.left > xoffs) LICE_FillRect(drawbm,xoffs,yoffs,cr.left-xoffs,h,col,al,LICE_BLIT_MODE_COPY);
      if (cr.right < xoffs+w)  LICE_FillRect(drawbm,cr.right,yoffs,xoffs+w - cr.right,h,col,al,LICE_BLIT_MODE_COPY);

      int vsw=min(xoffs+w,cr.right)-max(xoffs,cr.left),vsx=max(xoffs,cr.left);
      if (cr.top > yoffs) LICE_FillRect(drawbm,vsx,yoffs,vsw,cr.top - yoffs,col,al,LICE_BLIT_MODE_COPY);
      if (cr.bottom < yoffs+h) LICE_FillRect(drawbm,vsx,cr.bottom,vsw,yoffs+h-cr.bottom,col,al,LICE_BLIT_MODE_COPY);

      col=LICE_RGBA(128,128,128,255);
      al = 0.25f;
      LICE_Line(drawbm,cr.left,yoffs,cr.left,yoffs+h,col,al,LICE_BLIT_MODE_COPY,false);
      LICE_Line(drawbm,cr.right,yoffs,cr.right,yoffs+h,col,al,LICE_BLIT_MODE_COPY,false);
      LICE_Line(drawbm,xoffs,cr.top,xoffs+w,cr.top,col,al,LICE_BLIT_MODE_COPY,false);
      LICE_Line(drawbm,xoffs,cr.bottom,xoffs+w,cr.bottom,col,al,LICE_BLIT_MODE_COPY,false);

      LICE_DrawRect(drawbm,cr.left,cr.top,cr.right-cr.left,cr.bottom-cr.top,LICE_RGBA(255,255,255,255),1.0f,LICE_BLIT_MODE_COPY);

      
      g_imagerecord_font.SetCombineMode(LICE_BLIT_MODE_COPY,1.0f);

      {
        int w=m_croprect.right-m_croprect.left;
        int h=m_croprect.bottom-m_croprect.top;
        if (w<1) w= m_srcimage_w;
        if (h<1) h=m_srcimage_h;

        if (m_rot&1) 
        {
          int a=w;
          w=h;
          h=a;
        }
        RECT tr=r;
        tr.top+=24;

        double ar = w / (double)h;

        double bestdiff = 0.2;
        int denom=1;

        int i;
        static char denoms[] = { 1, 2,3,4,9,10};
        for(i=0;i<sizeof(denoms)/sizeof(denoms[0]);i++)
        {
          double b = ar*(int)denoms[i];
          b = fabs(floor(b+0.5) - b);
          if (b < bestdiff)
          {
            bestdiff=b;
            denom=(int)denoms[i];
          }
        }
        double num= ar * denom;
        // calculate ideal numerator/denom


        char numstr[64];
        sprintf(numstr,"%.1f",num);
        char *p=strstr(numstr,".");
        if (p && p[0] && p[1] == '0' && !p[2]) *p=0;

        char fmtstring[256];
        sprintf(fmtstring,"%dx%d -- %s : %d -- %.1fmpix",w,h,numstr,denom,w*(double)h/1000000.0);
        g_imagerecord_font.DrawText(drawbm,fmtstring,-1,&tr,DT_CENTER|DT_TOP|DT_SINGLELINE);
      }


    }

  }

  if (m_state != IR_STATE_LOADED && !srcimage)
  {
    const char *str= "ERROR";
    switch (m_state)
    {
      case IR_STATE_NEEDLOAD:
        str="loading";
      break;
      case IR_STATE_DECODING:
        str="decoding";
      break;
    }

    if (str[0])
    {
      g_imagerecord_font.SetCombineMode(LICE_BLIT_MODE_COPY,1.0f);
      g_imagerecord_font.DrawText(drawbm,str,-1,&r,DT_VCENTER|DT_CENTER|DT_SINGLELINE);
    }

  }

  g_imagerecord_font.SetCombineMode(LICE_BLIT_MODE_COPY,0.5f);

  RECT tr={0,};
  g_imagerecord_font.DrawText(drawbm,m_outname.Get(),-1,&tr,DT_CALCRECT|DT_SINGLELINE);

  if (tr.right-tr.left<8) tr.right=tr.left+8;
  if (tr.right-tr.left > r.right-r.left) tr.right=r.left + (r.right-r.left);
  tr.top = r.bottom - (tr.bottom) - 2;
  tr.bottom = r.bottom - 2;
  int xp = (r.right+r.left)/2 - (tr.right-tr.left)/2;
  tr.left += xp;
  tr.right += xp;

  g_imagerecord_font.DrawText(drawbm,m_outname.Get(),-1,&tr,DT_CENTER|DT_SINGLELINE);

  tr.left-=origin_x + m_position.left;
  tr.right-=origin_x + m_position.left;
  tr.top-=origin_y + m_position.top;
  tr.bottom-=origin_y + m_position.top;

  m_lastlbl_rect = tr;

  WDL_VWnd::OnPaint(drawbm,origin_x,origin_y,cliprect);
}

void ImageRecord::GetSizeInfoString(char *buf, int bufsz) // WxH or WxH cropped to WxH
{
  char str[1024];
  int w=m_croprect.right-m_croprect.left;
  int h=m_croprect.bottom-m_croprect.top;
  int srcw=m_srcimage_w;
  int srch=m_srcimage_h;
  if (w<1) w=srcw;
  if (h<1) h=srch;

  if (m_rot&1)
  {
    int a=w; w=h; h=a;
    a=srcw; srcw=srch; srch=a;
  }

  sprintf(str,"%dx%d",srcw,srch);
  if (w!=srcw||h!=srch) sprintf(str+strlen(str)," cropped to %dx%d",w,h);

  lstrcpyn(buf,str,bufsz);
}