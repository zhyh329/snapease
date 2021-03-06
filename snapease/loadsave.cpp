/*
    SnapEase
    loadsave.cpp -- loading/saving of image lists
    Copyright (C) 2009 and onward Cockos Incorporated

    SnapEase is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    SnapEase is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with SnapEase; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "main.h"
#include <math.h>
#include "../WDL/projectcontext.h"
#include "../WDL/lineparse.h"

bool g_imagelist_fn_dirty; // need save
WDL_FastString g_imagelist_fn;
static const char *imagelist_extlist="Snapease image lists (*.SnapeaseList)\0*.SnapeaseList\0All Files\0*.*\0\0"; 


WDL_INT64 file_size(const char *filename)
{
#ifdef _WIN32
  HANDLE hFile=CreateFile(filename,0,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
  if (hFile == INVALID_HANDLE_VALUE) return -1;
  DWORD l,h;
  l=GetFileSize(hFile,&h);
  CloseHandle(hFile);
  return ((WDL_INT64)h)<<32 | l;
#else
#if 1
  struct stat sb;
  if (!stat(filename,&sb)) return sb.st_size; // this is 64-bit on osx, yay!
  return -1;
#else
  FILE *fp=fopen(filename,"rb");
  if (!fp) return -1;
  fseek(fp,0,SEEK_END);
  int pos=ftell(fp);
  fclose(fp);
  return pos;
#endif
#endif
}

bool file_exists(const char *filename)
{
#ifdef _WIN32
  HANDLE hFile=CreateFile(filename,0,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
  if (hFile == INVALID_HANDLE_VALUE) return false;
  CloseHandle(hFile);
#else
#if 1
  struct stat sb;
  if (stat(filename,&sb)) return false;
#else
  FILE *fp=fopen(filename,"rb");
  if (!fp) return false;
  fclose(fp);
#endif
#endif

  return true;
}


static void make_fn_relative(const char *leadpath, const char *in, char *out, int outlen) // leadpath must NOT have any trailing \\ or /
{
  size_t pdlen=0;
  if (1)
  {
    pdlen=strlen(leadpath);

    if (pdlen && (strlen(in)<=pdlen || strnicmp(leadpath,in,pdlen) || !WDL_IS_DIRCHAR(in[pdlen]))) pdlen=0;
  }

  lstrcpyn(out,in+pdlen,outlen);
}

static void resolve_fn_fromrelative(const char *leadpath, const char *in, char *out, int outlen)
{
  if (
#ifdef _WIN32
    in[0] && in[1] != ':' && (in[0]!='\\' || in[1] != '\\') &&
#else
    in[0] && in[0] != '/' &&
#endif
    !file_exists(in)
    
    ) 
  {
    WDL_FastString tmp(leadpath);
    tmp.Append(PREF_DIRSTR);
    tmp.Append(in);
    if (file_exists(tmp.Get()))
    {
      lstrcpyn(out,tmp.Get(),outlen);
      return;
    }
  }

  lstrcpyn(out,in,outlen);
}

bool importImageListFromFile(const char *fn, bool addToCurrent)
{

  ProjectStateContext *ctx = ProjectCreateFileRead(fn);
  if (!ctx) return false;

  WDL_FastString leadpath(fn);
  leadpath.remove_filepart();

  bool cstate=false;
  LineParser lp(cstate);
  
  bool success=false;
  int itemsAdded=0;
  bool hasCleared=false;
  bool inListBlock=false;

  ImageRecord *activitem=NULL;
  while (ProjectContext_GetNextLine(ctx,&lp))
  {
    if (inListBlock)
    {
      if (lp.gettoken_str(0)[0]=='>')
      {
        success=true;
        break;
      }
      if (lp.gettoken_str(0)[0]=='<')
      {
        if (!ProjectContext_EatCurrentBlock(ctx)) break; // fail!
      }
      else
      {
        if (!stricmp(lp.gettoken_str(0),"IMAGE") ||
            !stricmp(lp.gettoken_str(0),"IMAGE_FULL"))
        {
          bool activate = !addToCurrent && !stricmp(lp.gettoken_str(0),"IMAGE_FULL");
          if (lp.getnumtokens()>9)
          {
            char resfn[4096];
            resolve_fn_fromrelative(leadpath.Get(),lp.gettoken_str(1),resfn,sizeof(resfn));
            ImageRecord *rec= new ImageRecord(resfn, lp.getnumtokens()>16 ? (time_t) lp.gettoken_float(16) : 0);
            rec->m_outname.Set(lp.gettoken_str(2));
            rec->m_bw = !!lp.gettoken_int(3);
            rec->m_rot = lp.gettoken_int(4)&3;
            if (activate) g_edit_mode = lp.gettoken_int(5);
            rec->m_croprect.left = lp.gettoken_int(6);
            rec->m_croprect.top = lp.gettoken_int(7);
            rec->m_croprect.right = lp.gettoken_int(8);
            rec->m_croprect.bottom = lp.gettoken_int(9);
            if (lp.getnumtokens()>14)
            {
              rec->m_bchsv[0] = (float)lp.gettoken_float(10);
              rec->m_bchsv[1] = (float)lp.gettoken_float(11);
              rec->m_bchsv[2] = (float)lp.gettoken_float(12);
              rec->m_bchsv[3] = (float)lp.gettoken_float(13);
              rec->m_bchsv[4] = (float)lp.gettoken_float(14);
            }
            if (lp.getnumtokens() > 15) rec->m_need_rotchk = !!lp.gettoken_int(15);
            else rec->m_need_rotchk = false;

            rec->UpdateButtonStates();

            AddImageRec(rec);


            itemsAdded++;

            if (activate && !activitem) activitem=rec;

          }

        }
      }

      // process our lines
    }
    else // at top level
    {
      if (lp.gettoken_str(0)[0]=='<')
      {
        if (!stricmp(lp.gettoken_str(0),"<SNAPEASE_IMAGELIST"))
        {
          if (!addToCurrent && !hasCleared)
          {
            hasCleared = true;
            ClearImageList();
          }
          inListBlock=true;
        }
        else
          if (!ProjectContext_EatCurrentBlock(ctx)) break; // fail!
      }
    }   
  }

  if (!addToCurrent) 
  {
    g_imagelist_fn.Set(success ? fn : "");
    g_imagelist_fn_dirty = !success;
  }
  else
  {
    g_imagelist_fn_dirty = !!itemsAdded;
  }
  if (activitem&&g_images.Find(activitem)>=0) OpenFullItemView(activitem);
  else
    RemoveFullItemView(false);
  UpdateMainWindowWithSizeChanged();
  UpdateCaption();


  delete ctx;
  return !!success;
}

bool saveImageListToFile(const char *fn)
{
  ProjectStateContext *ctx = ProjectCreateFileWrite(fn);
  if (!ctx) return false;

  WDL_FastString leadpath(fn);
  leadpath.remove_filepart();

  int x;
  ctx->AddLine("<SNAPEASE_IMAGELIST 0.0");
  WDL_String tbuf;
  for (x=0;x<g_images.GetSize();x++)
  {
    ImageRecord *rec = g_images.Get(x);
    char buf[4096];
    make_fn_relative(leadpath.Get(),rec->m_fn.Get(),buf,sizeof(buf));
    makeEscapedConfigString(rec->m_outname.Get(),&tbuf);

    ctx->AddLine("%s \"%s\" %s %d %d %d %d %d %d %d %f %f %f %f %f %d %.0f",
        rec == g_fullmode_item ? "IMAGE_FULL" : "IMAGE", 
        buf,
        tbuf.Get(),
        rec->m_bw,
        rec->m_rot,
        g_edit_mode,
        rec->m_croprect.left,
        rec->m_croprect.top,
        rec->m_croprect.right,
        rec->m_croprect.bottom,
        rec->m_bchsv[0],
        rec->m_bchsv[1],
        rec->m_bchsv[2],
        rec->m_bchsv[3],
        rec->m_bchsv[4],
        rec->m_need_rotchk,
        (double)rec->m_file_timestamp
        );
        
  }
  ctx->AddLine(">");

  WDL_INT64 sz = ctx->GetOutputSize();
  delete ctx;
  return file_size(fn) == sz;

}



void LoadImageList(bool addToCurrent)
{
  char temp[1024];
  lstrcpyn(temp,g_imagelist_fn.Get(),sizeof(temp));
  
#ifdef _WIN32
  OPENFILENAME l={sizeof(l),};
  l.hwndOwner = g_hwnd;
  l.lpstrFilter = (char *)imagelist_extlist;
  l.lpstrFile = temp;
  l.nMaxFile = sizeof(temp)-1;
  l.lpstrTitle = "Choose file to open:";
  l.lpstrDefExt = "";

  l.lpstrInitialDir=g_list_path.Get();

  l.Flags = OFN_HIDEREADONLY|OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
  if (GetOpenFileName(&l)) 
  {
#else
  char *p;
  if ((p=BrowseForFiles("Choose file to open:",g_list_path.Get(),temp,false,imagelist_extlist)))
  {
    lstrcpyn(temp,p,sizeof(temp));
    free(p);
#endif
    if (!importImageListFromFile(temp,addToCurrent))
      MessageBox(g_hwnd,"Error reading some or all of image list!","Error loading",MB_OK);

  }
}

bool SaveImageList(bool forceSaveAs)
{
  if (forceSaveAs || !g_imagelist_fn.Get()[0])
  {
    char temp[1024];
    lstrcpyn(temp,g_imagelist_fn.Get(),sizeof(temp));
    
#ifdef _WIN32
    OPENFILENAME l={sizeof(l),};
    l.hwndOwner = g_hwnd;
    l.lpstrFilter = (char*)imagelist_extlist;
    l.lpstrFile = temp;
    l.nMaxFile = sizeof(temp)-1;
    l.lpstrTitle = "Choose file to save:";
    l.lpstrDefExt = "";
    l.lpstrInitialDir=g_list_path.Get();

    l.Flags = OFN_HIDEREADONLY|OFN_EXPLORER|OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR;
    if (GetSaveFileName(&l)) 
#else
    if (BrowseForSaveFile("Choose file to save:",g_list_path.Get(),temp,imagelist_extlist,temp,sizeof(temp)))
#endif
    {
      g_imagelist_fn.Set(temp);
    }
    else
    {
      return false;
    }

  }

  if (saveImageListToFile(g_imagelist_fn.Get()))
  {
    g_imagelist_fn_dirty=false;
    UpdateCaption();

    return true;
  }

  MessageBox(g_hwnd,"Error writing to output file","Error saving",MB_OK);
  return false;
}

bool SavePromptForClose(const char *promptmsg)
{
  if (!g_imagelist_fn_dirty) return true;

  if (!g_images.GetSize() && !g_imagelist_fn.Get()[0]) return true; // no images + no file = not dirty

  char buf[4096];
  sprintf(buf,"%s\r\n\r\n(Selecting \"no\" will result in unsaved changes being lost!)",promptmsg);
  int a = MessageBox(g_hwnd,buf,"Save changes?",MB_YESNOCANCEL);

  if (a == IDCANCEL) return false;

  return a == IDNO || SaveImageList(false); // if saved, proceed, otherwise, cancel
}

bool IsImageListDirty()
{
  return g_imagelist_fn_dirty && (g_images.GetSize()  || g_imagelist_fn.Get()[0]);
}

void SetImageListIsDirty(bool isDirty)
{
  if (IsImageListDirty()!=isDirty)
  {
    g_imagelist_fn_dirty=isDirty;
    UpdateCaption();
  }
}

void UpdateCaption()
{
  static WDL_FastString tmp;
  if (g_imagelist_fn.GetLength())
  {
    tmp.Set(g_imagelist_fn.get_filepart());
    tmp.remove_fileext();

    if (IsImageListDirty()) tmp.Append(" [modified]");
  }
  else
  {
    if (IsImageListDirty()) tmp.Set("[unsaved image list]");
  }
  
  if (tmp.Get()[0]) tmp.Append(" - ");

  tmp.Append("SnapEase");

  SetWindowText(g_hwnd,tmp.Get());

  #ifndef _WIN32
  SWELL_SetWindowRepre(g_hwnd,g_imagelist_fn.Get(),IsImageListDirty());
  #endif

}
