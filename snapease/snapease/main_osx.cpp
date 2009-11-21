
#include "../../WDL/swell/swell.h"

#include "../resource.h"
#include "../../WDL/swell/swell-dlggen.h"

#include "../main.h"

extern HMENU SWELL_app_stocksysmenu;

char g_exepath[4096];

INT_PTR SWELLAppMain(int msg, INT_PTR parm1, INT_PTR parm2)
{
  switch (msg)
  {
    case SWELLAPP_ONLOAD:
      {
        GetModuleFileName(NULL,g_exepath,sizeof(g_exepath));
        char *p=g_exepath;
        while (*p) p++;
        while (p > g_exepath && *p != '/') p--; *p=0;
      }
      g_ini_file.Set(g_exepath);
      g_ini_file.Append("/snapease.ini");      
    break;
    case SWELLAPP_LOADED:
      if (SWELL_app_stocksysmenu)
      {
        HMENU menu = CreatePopupMenu();    
        HMENU nm=SWELL_DuplicateMenu(SWELL_app_stocksysmenu);
        if (nm)
        {
          MENUITEMINFO mi={sizeof(mi),MIIM_STATE|MIIM_SUBMENU|MIIM_TYPE,MFT_STRING,0,0,nm,NULL,NULL,0,"SnapEase"};
          InsertMenuItem(menu,0,TRUE,&mi);           
        }    
        SWELL_SetDefaultModalWindowMenu(menu);
      }      
      {
        HMENU menu = LoadMenu(NULL,MAKEINTRESOURCE(IDR_MENU1));
        {
          HMENU sm=GetSubMenu(menu,0);
          DeleteMenu(sm,ID_QUIT,MF_BYCOMMAND);
          
          int a= GetMenuItemCount(sm);
          while (a > 0 && GetMenuItemID(sm,a-1)==0)
          {
            DeleteMenu(sm,a-1,MF_BYPOSITION);
            a--;
          }
        }
        
        // set modifiers
        SetMenuItemModifier(menu,ID_EXPORT,MF_BYCOMMAND,'E',FCONTROL);
        SetMenuItemModifier(menu,ID_IMPORT,MF_BYCOMMAND,'A',0);
        SetMenuItemModifier(menu,ID_NEWLIST,MF_BYCOMMAND,'N',FCONTROL);
        SetMenuItemModifier(menu,ID_LOAD,MF_BYCOMMAND,'O',FCONTROL);
        SetMenuItemModifier(menu,ID_LOAD_ADD,MF_BYCOMMAND,'O',FCONTROL|FSHIFT);
        SetMenuItemModifier(menu,ID_SAVE,MF_BYCOMMAND,'S',FCONTROL);
        SetMenuItemModifier(menu,ID_SAVEAS,MF_BYCOMMAND,'S',FCONTROL|FSHIFT);  
        
        if (SWELL_app_stocksysmenu)
        {
          HMENU nm=SWELL_DuplicateMenu(SWELL_app_stocksysmenu);
          if (nm)
          {
            MENUITEMINFO mi={sizeof(mi),MIIM_STATE|MIIM_SUBMENU|MIIM_TYPE,MFT_STRING,0,0,nm,NULL,NULL,0,"SnapEase"};
            InsertMenuItem(menu,0,TRUE,&mi);           
          }
        }  
        
        HWND h=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_MAIN),NULL,MainWindowProc);
        
        
        SetMenu(h,menu);
      }
    break;
    case SWELLAPP_ONCOMMAND:
      if (g_hwnd) SendMessage(g_hwnd,WM_COMMAND,parm1&0xffff,0);
    break;
    case SWELLAPP_DESTROY:
      if (g_hwnd) DestroyWindow(g_hwnd);
    break;
    case SWELLAPP_PROCESSMESSAGE:
      if (MainProcessMessage((MSG*)parm1)>0) return 1;
    return 0;
  }
  return 0;
}


#include "../snapease.rc_mac_dlg"

#undef BEGIN
#undef END
#include "../../WDL/swell/swell-menugen.h"

#include "../snapease.rc_mac_menu"
