// ********************************************************************************
// **
// ** Original link:
// ** https://www.codeproject.com/Articles/9434/Automatic-resizing-controls
// **
// ** Helper Class / macros to implement docking / anchoring for controls within
// ** windows and dialogs. Works with all window classes in ATL, MFC and pure
// ** Win32
// ** 
// ** This source is freeware. In all cases, NO fee must be charged for this piece
// ** of code, for any reason. 
// ** You use this at your own risk !
// **
// ** greetz!, 
// ** A. Thiede / BluePearl Software aka. drice
// **
// ** Remarks: 
// **   - Revamped the entire code to add modern C++ features, (yikes, vectors), 
// **     removed previous hardcoded control limits, added scoping to methods to avoid
// **     usage confusion, separated header from code for faster compilations of
// **     projects using precompiled headers, moved MOST global variables to individual 
// **     list items and now added support for child windowses inside containers in the 
// **     same windows. 
// **     (Like windowsews inside tabbed controls that won't auto-move or auto-resize
// **      if the original control resized).
// **   - Also the class can now call static methods to recalculate/reposition controls
// **     that were originally part of a subdialog that now must fit inside a new 
// **     window or container (like a tab control), all respecting the given docking /
// **     anchoring behaviors.
// **   By: Leonardo Silva
// ********************************************************************************

// ============================================================================
// USAGE:
//
// - include DECLARE_ANCHOR_MAP() within your window/dialog class declaration
//
// - include the other macros within the implementation file (outside your class)
//
//   example:   BEGIN_ANCHOR_MAP(CMyDialogClass)
//                ANCHOR_MAP_CHILDWINDOW(hWndWindowInContainer, ANF_ALL)
//                ANCHOR_MAP_ENTRY(hWndParent, IDOK,         ANF_BOTTOM)
//                ANCHOR_MAP_ENTRY(hWndParent, IDCANCEL,     ANF_BOTTOM)
//                ANCHOR_MAP_ENTRY(hWndParent, IDC_EDITCTRL, ANF_TOP | RIGHT | ANF_BOTTOM)
//                ANCHOR_MAP_ENTRY(hWndParent, NULL,         ANF_AUTOMATIC)
//              END_ANCHOR_MAP(YourMainWindowHandle)
// 
// - Within your WM_SIZE handler, call HandleAnchors You can additionally
//   call Invalidate(false) after calling HandleAnchors to ensure that there are
//   no painting/updating problems when the contorls have been moved.
//
// - If you DECLARE_ANCHOR_MAP() in your class, you can then put the macro 
//   ANCHOR_MAP_EREASEBACKGROUND() to handle WM_EREASEBKGND messages to ensure
//   proper screens update upon control resizing.
// 
// - You can #define USE_ANF_SCREEN_TO_CLIENT before including the header for a 
//   RECT supporting version of Win32 API ScreenToClient (originally takes 
//   POINT structs);
// 
// - There are some new CLASS-level (static) overloaded function helpers to use 
//   when the target window of your controls is created externally and you need to 
//   reposition / anchor controls using the original screen design. 
//   They are listed bellow:
// 
//          - ControlAnchorMap::applyMargins
//          - ControlAnchorMap::calculateMargins
//          - ControlAnchorMap::calculateOriginalMargins
//          - ControlAnchorMap::repositControl
// 
//   Please see the documentation for each on the method's header.
//
// That�s it.
//
// =============================================================================

#pragma once

// #define DEBUG_ANCHORLIB       // Enables passing user-defined names for controls to debug this class



    // ===================== Anchoring / Docking flags ======================


// Note: You should not combine docking (ANF_DOCK_...) and anchoring flags.
//       You can try it, but it�s not tested.
//       Additionally, ANF_AUTOMATIC should not be used with any other flags...
//       In fact, ANF_AUTOMATIC WILL override other flags for the objects upon initialization.

#define ANF_NONE            0x0000                  /* proportinally moves the control with the size of the window */
#define ANF_DOCK_TOP        0x0001                  /* docks the control to the top of the window */
#define ANF_DOCK_BOTTOM     0x0002                  /* docks the control to the bottom of the window */
#define ANF_DOCK_LEFT       0x0004                  /* docks the control to the left of the window */
#define ANF_DOCK_RIGHT      0x0008                  /* docks the control to the right of the window */
#define ANF_DOCK_ALL        0x000F                  /* docks to all positions - filling the entire client area */
#define ANF_TOP             0x0010                  /* distance of the control to the top of the window will be constant */
#define ANF_BOTTOM          0x0020                  /* distance of the control to the bottom of the window will be constant */
#define ANF_LEFT            0x0040                  /* distance of the control to the left of the window will be constant */
#define ANF_RIGHT           0x0080                  /* distance of the control to the right of the window will be constant */
#define ANF_AUTOMATIC       0x0100                  /* automatically calculate the anchors, cannot be used with other flags */
#define ANF_DOCK_TOP_EX     0x0200                  /* docks the top of the control to the top of the window */
#define ANF_DOCK_BOTTOM_EX  0x0400                  /* docks the bottom of the control to the bottom of the window */
#define ANF_DOCK_LEFT_EX    0x0800                  /* docks the left-side of the control to the left-side of the window */
#define ANF_DOCK_RIGHT_EX   0x1000                  /* docks the right-side of the control to the right-side of the window */

// some additional control flags
#define ANF_ERASE           0x2000                  /* forces to erase the background of the control in EraseBackground */

// some combinations
#define ANF_TOPLEFT         (ANF_TOP | ANF_LEFT)              /* combined anchors TOP and LEFT */
#define ANF_TOPRIGHT        (ANF_TOP | ANF_RIGHT)             /* combined anchors TOP and RIGHT */
#define ANF_BOTTOMLEFT      (ANF_BOTTOM | ANF_LEFT)           /* combined anchors BOTTOM and LEFT */
#define ANF_BOTTOMRIGHT     (ANF_BOTTOM | ANF_RIGHT)          /* combined anchors BOTTOM and RIGHT */
#define ANF_TOPBOTTOM       (ANF_TOP | ANF_BOTTOM)            /* combined anchors TOP and BOTTOM */
#define ANF_LEFTRIGHT       (ANF_LEFT | ANF_RIGHT)            /* combined anchors LEFT and RIGHT */
#define ANF_ALL             (ANF_TOPLEFT | ANF_BOTTOMRIGHT)   /* anchors the control to all sides of the window */    

// Flags for InitAnchors()
#define ANIF_CALCSIZE       0x0001                  /* calculate size occupied by all controls, useful for formviews */
#define ANIF_SIZEGRIP       0x0002                  /* add a sizing-grip to the parent window */

// Flags for ApplyMargins
#define MARGIN_LEFT         0x0001
#define MARGIN_TOP          0x0002
#define MARGIN_RIGHT        0x0004
#define MARGIN_BOTTOM       0x0008
#define MARGIN_TOPLEFT      MARGIN_TOP | MARGIN_LEFT  
#define MARGIN_TOPRIGHT     MARGIN_TOP | MARGIN_RIGHT
#define MARGIN_BOTTOMLEFT   MARGIN_BOTTOM | MARGIN_LEFT
#define MARGIN_BOTTOMRIGHT  MARGIN_BOTTOM | MARGIN_RIGHT
#define MARGIN_TOPBOTTOM    MARGIN_TOP | MARGIN_BOTTOM
#define MARGIN_LEFTRIGHT    MARGIN_LEFT | MARGIN_RIGHT
#define MARGIN_ALL          0x000F

// Flags for InvertMargins
#define INVERT_HORIZONTAL   0x0001
#define INVERT_VERTICAL     0x0002
#define INVERT_BOTH         0x0003

    // =============== Some structures for the audience... ==================


// A margin-defining rectangle
struct OFFSETRECT {  
    LONG leftMargin = 0;
    LONG topMargin = 0;
    LONG rightMargin = 0;
    LONG bottomMargin = 0;
};

// A floating-point RECT structure
struct FRECT {  
    double  top = 0;
    double  left = 0;
    double  right = 0;
    double  bottom = 0;
};

// A floating-point SIZE structure
struct FSIZE {  
    double  cx = 0;
    double  cy = 0;
};


class ControlAnchorMap {

protected:

    // ======================== INTERNAL STRUCTURES =========================

    struct TCtrlEntry {
#ifdef DEBUG_ANCHORLIB
        std::string     nControlName;                                 // control name - for debbugging only
#endif
        HWND            hWnd = nullptr;                               // hWnd of the control
        HWND            hWndParent = nullptr;                         // handle of the window that contains the control/window
        unsigned int    nCtrlID = 0;                                  // dialog-control resource id of the window
        unsigned int    nFlags = 0;                                   // docking/anchoring flags for this control
        FRECT           rect = { 0, 0, 0, 0 };                        // actual client-rectangle of the control
        RECT            parentPrevWindowRect = { 0, 0, 0, 0 };        // previous rect of the parent window (old m_rcPrev)
        RECT            parentClientRect = { 0, 0, 0, 0 };            // client area of child window's parent (old m_rcClient)
        RECT            parentNewRect = { 0, 0, 0, 0 };               // current window-rect of the parent window (old m_rcNew)
        SIZE            szDelta = { 0, 0 };                           // delta of the size-change
        unsigned int    uiSizedBorders = 0;                           // Flags for borders that have been sized (previous m_uiSizedBorders)
    };

    // ============================ MEMBERS =================================


    bool                         m_bInitialized;             // class and control-information is inited
    HWND                         m_globalParent;             // used to grab controls when user didn't specify which ones he wanted auto-handled
    bool                         m_bUsedDefaultEntry;        // "default" (NULL) anchor-map entry has been used
    unsigned int                 m_nDefaultFlags;            // flags for "default" controls
    COLORREF                     m_clrBackground;            // background-color of the dialog
    HWND                         m_hWndSizeGrip;             // handle of the sizing-grip or NULL
    std::vector<TCtrlEntry>      m_Controls;                 // control-map
    bool                         m_isSorted = false;         // checks if a sort was already done

public:


    // ========================= CLASS METHODS ==============================


    // ======================================================================
    // constructor
    // ======================================================================
    ControlAnchorMap() :
        m_bInitialized(0), m_bUsedDefaultEntry(0), m_globalParent(nullptr),
        m_nDefaultFlags(0), m_hWndSizeGrip(0), m_isSorted(false)
    {
        m_clrBackground = GetSysColor(COLOR_BTNFACE);
    };

    // ======================================================================
    // Adds a child window for docking/anchoring -> eg: a dialog loaded inside
    // a control container - like a Tab Control. You must call this before 
    // calling Initialize(). A call to this function is wrapped within the 
    // ANCHOR_MAP_CHILDWINDOW function.
    // ======================================================================
#ifdef DEBUG_ANCHORLIB
    bool AddChildWindow(HWND window, unsigned int flags, std::string name);
#else
    bool AddChildWindow(HWND window, unsigned int flags);
#endif

    // ======================================================================
    // Adds a control for docking/anchoring. You must call this before calling
    // Initialize() A call to this function is wrapped within the 
    // ANCHOR_MAP_ENTRY function.
    // ======================================================================
#ifdef DEBUG_ANCHORLIB
    bool AddControl(HWND parent, unsigned int ctrlID, unsigned int flags, std::string name);
#else
    bool AddControl(HWND parent, unsigned int ctrlID, unsigned int flags);
#endif

    // ======================================================================
    // Setups the class to use the some Default Flags for unassigned controls.
    // Use it BEFORE Initialize().
    // ======================================================================
    void UseDefaultFlags(unsigned int nFlags);

    // ======================================================================
    // Returns true if the information for the parent-window and the
    // controls has been initialized, false otherwise
    // ======================================================================
    bool IsInitialized();

    // ======================================================================
    // Initializes the class-members, gets window locations and information
    // about the controls in the control-map (m_Ctrls).
    // dwFlags is a combination of ANIF_ flags
    // ======================================================================
    void Initialize(HWND hWndGlobalParent, DWORD dwFlags);

    // ======================================================================
    // Does the actual anchoring/docking processing.
    // Note that docking-flags (ANF_DOCK_...) have a higher privilege
    // than the normal anchoring flags. If an ANF_DOCK_... flag is specified
    // for a control, the control is docked and no anchoring is done !
    // [in] : pWndRect = new rectangle of the resized window (use GetWndRect())
    //                   If you pass NULL, the function will call GetWndRect()
    //                   itself.
    // Requirements: calls to this functions MUST have Initialized()
    //               before... assertions for throwing errors on uinitialized
    //               usage.
    // ======================================================================
    void HandleAnchors();

    // ======================================================================
    // This is an enhanced EraseBackground-function which only erases the
    // background "around" the controls to remove the flicker when the
    // window is resized. Call this function from your OnEraseBackground-
    // (WM_ERASEBACKGROUND)-message handler instead of the default-
    // implementation
    // ======================================================================
    bool EraseBackground(HDC hDC);

    // ======================================================================
    // Clears the whole system...
    // ======================================================================
    void Reset();

    // ======================================================================
    // Sets default background for erease operations...
    // ======================================================================
    void SetClearBackgroundColor(COLORREF newColor);


    // ========================= STATIC METHODS =============================


    // ======================================================================
    // ScreenToClientH helper-function 
    // ======================================================================
    static bool ScreenToClientH(HWND hWnd, RECT* pRect);

    static void CopyOffsetRect(OFFSETRECT& target, const OFFSETRECT& origin)
    {
        target.leftMargin = origin.leftMargin;
        target.topMargin = origin.topMargin;
        target.rightMargin = origin.rightMargin;
        target.bottomMargin = origin.bottomMargin;
    }

    // ======================================================================
    // Applies the margins from an OFFSETRECT into a RECT struct...
    // ======================================================================
    static void applyMargins(const OFFSETRECT& margins, RECT& target, int marginFlags = MARGIN_ALL) {
        if (marginFlags & MARGIN_LEFT)
            target.left += margins.leftMargin;
        if (marginFlags & MARGIN_TOP)
            target.top += margins.topMargin;
        if (marginFlags & MARGIN_RIGHT)
            target.right += margins.rightMargin;
        if (marginFlags & MARGIN_BOTTOM)
            target.bottom += margins.bottomMargin;
    }

    // ======================================================================
    // Helper method to calculate the offset margins between two rectangles 
    // (makes more sense for a smaller rectangle inside a big one but will 
    // work for any rect because it can return negative margin numbers).
    // Here, A would be the bigger, B would be the smaller.
    // ======================================================================
    static OFFSETRECT calculateMargins(const RECT& a, const RECT& b) {
        return  { b.left - a.left, b.top - a.top, b.right - a.right, b.bottom - a.bottom };
    }

    // ======================================================================
    // Helper method to calculate the reverse offset margins between two 
    // rectangles. Makes more sense for a smaller rectangle inside a big one 
    // and when you want margins to expand instead of contract.
    // This is when you want object margins to expand instead of contract.
    // It's the same to call (calculateMargins(B, A) x -1)  instead of (A, B).
    // ======================================================================
    static OFFSETRECT calculateReverseMargins(const RECT& a, const RECT& b) 
    {
        return { a.left - b.left, a.top - b.top, a.right - b.right, a.bottom - b.bottom };
    }

    // ======================================================================
    // Inverts a offset rectangle reference. Flags are:
    // INVERT_HORIZONTAL, INVERT_VERTICAL, INVERT_BOTH.
    // ======================================================================
    static void invertOffsetRect(OFFSETRECT& marginRect, int flags = INVERT_BOTH) 
    {
        OFFSETRECT resultOffset = marginRect;
        if (flags & INVERT_HORIZONTAL)
        {
            resultOffset.leftMargin = marginRect.rightMargin;
            resultOffset.rightMargin = marginRect.leftMargin;
        }
        if (flags & INVERT_VERTICAL)
        {
            resultOffset.topMargin = marginRect.bottomMargin;
            resultOffset.bottomMargin = marginRect.topMargin;
        }

        CopyOffsetRect(marginRect, resultOffset);
    }


    // ======================================================================
    // Displaces the given marginRect in the displacements points X and Y.
    // ======================================================================
    static void moveOffsetRect(OFFSETRECT& marginRect, const POINT& displacement)
    {
        marginRect.topMargin += displacement.x;
        marginRect.rightMargin += displacement.x;
        marginRect.topMargin += displacement.y;
        marginRect.bottomMargin += displacement.y;
    }

    // ======================================================================
    // Calculate the original margin OFFSETS of a given controlID inside a 
    // compiled Dialog resource persisted in a module (eg: returns the margins 
    // an Edit Control named IDC_EDIT1 inside a IDD_DIALOG had originally when 
    // the Dialog was designed).
    // RETURNS: false if the dialog or the control aren't found for the given 
    // module.    
    // ======================================================================
    static bool calculateOriginalMargins(HINSTANCE parent, int dialogID, int controlID, OFFSETRECT& output);

    // ======================================================================
    // Calculate the original margin OFFSETS of a given control inside a 
    // compiled Dialog resource persisted in a module (eg: returns the margins 
    // an Edit Control named IDC_EDIT1 inside a IDD_DIALOG had originally when 
    // the Dialog was designed).
    // RETURNS: false if the dialog or the control aren't found for the given 
    // module.    
    // ======================================================================
    static bool calculateOriginalMargins(HWND parent, HWND childControl, OFFSETRECT& output);

    // ======================================================================
    // Reposits the targetControl inside a targetWindow client area, given 
    // you provide a pointer to the ORIGINAL design-time window (eg: one just 
    // created with CreateDialogEx functions and the original control ID 
    // inside that window, considering the provided docking/anchoring flags 
    // for the control.
    // ======================================================================
    static bool repositControl(HWND targetControl, HWND targetWindow, HWND originalWindow,
        HWND originalControl, int flags, const OFFSETRECT& additionalMargins = {0,0,0,0});

    // ======================================================================
    // Reposits the targetControl inside a targetWindow client area, given 
    // you provide a pointer to the ORIGINAL design-time window (eg: one just 
    // created with CreateDialogEx functions and the original control ID 
    // inside that window, considering the provided docking/anchoring flags 
    // for the control.
    // ======================================================================
    static bool repositControl(HWND targetControl, HWND targetWindow, HWND originalWindow,
        int originalControlID, int flags, const OFFSETRECT& additionalMargins = { 0,0,0,0 });

    // ======================================================================
    // Repositis the targetControl inside a targetWindow, given you provide
    // information about the ORIGINAL design-time window (eg: the Dialog 
    // within a given HMODULE and the original control ID inside that dialog,
    // considering the provided docking/anchoring flags for the control.
    // ======================================================================
    static bool repositControl(HWND targetControl, HWND targetWindow, HINSTANCE originalModule,
        int originalDialogID, int originalControlID, int flags, const OFFSETRECT& additionalMargins = { 0,0,0,0 });

private:

    // ======================================================================
    // Finds a window with the specified HWND in the control-map and 
    // returns its index or returns -1 if the window was not found
    // ======================================================================
    int FindWindow(HWND hWnd);

    // ======================================================================
    // Adds a control or window for docking/anchoring. Internal use...
    // Call AddControl or AddChildWindow instead.
    // ======================================================================
#ifdef DEBUG_ANCHORLIB
    bool AddObject(HWND windowOrParent, unsigned int nFlags, unsigned int nIDCtrl = 0, 
        bool isChildWindow = false, std::string nControlName = "");
#else
    bool AddObject(HWND windowOrParent, unsigned int nFlags, unsigned int nIDCtrl = 0, 
        bool isChildWindow = false);
#endif

    // ======================================================================
    // This function does the pre-processing for the calls to HandleAnchors.
    // It stores the new size of the parent-window within m_rcNew, determines
    // which side(s) of the window have been resized and sets the apropriate
    // flags in m_uiSizedBorders and then it calculates the deltas and the
    // new client-rectangle of the parent.
    // The calculated values are then used by HandleAnchors to move/resize
    // the controls.
    // [in]: pWndRect = new rectangle of the resized parent-window 
    //                  (use GetWndRect())
    // ======================================================================
    static void PreProcess(TCtrlEntry& pControl);

    // ======================================================================
    // This function does the post-processing for the calls to HandleAnchors.
    // It preserves the actual (new) size of the parent-window as "previous
    // size". In the next call to PreProcess, this "previons size" is used
    // to calulate the deltas
    // ======================================================================
    static void PostProcess(TCtrlEntry& pControl);

    // ======================================================================
    // Moves one object on the screen - a control or a window.
    // if useSetPosition is set to TRUE will move with SetPosition function
    // instead of the (default) DeferWindowPos behavior.
    // ======================================================================
    static void MoveObject(TCtrlEntry& pCtrl, HDWP hDeferPos, bool useSetPosition = false);

    // ======================================================================
    // SetFREct helper-function
    // ======================================================================
    static void SetFRect(FRECT* pRect, double left, double top, double right, double bottom);

    // ======================================================================
    // Child-window enumeration callback function.
    // This function is called from EnumChildWindows, which again is
    // called from Initialize if the "default option" has been used.
    // It adds the enumerated window to the control-list, if it is not
    // already there.
    // ======================================================================
    static int CALLBACK InitDefaultControl(HWND hWnd, LPARAM lParam);

};

// ============================================================================
//
// MACROS FOR THE PUBLIC :     
//
// DECLARE_ANCHOR_MAP declares the variable m_bpfxAnchorMap within your window
//                    class and declares the two functions InitAnchors and
//                    HandleAnchors
//
// BEGIN_ANCHOR_MAP   implements the two functions, declared by 
//                    DECLARE_ANCHOR_MAP
//
// ANCHOR_MAP_ENTRY   adds a control to the control-map and pass the 
//                    HWND of the parent and the ID of the control.
//
// ANCHOR_MAP_ENTRY_RANGE
//                    This is the same as ANCHOR_MAP_ENTRY except that it
//                    adds a range of controls to the control-map
// 
// ANCHOR_MAP_CHILDWINDOW
//                    Adds a window-inside-container to the anchoring/docking
//                    mechanism. Usefull for tabbed dialogs.
//
// END_ANCHOR_MAP     finishes the implementation of InitAnchors and calls
//                    the initialization routine of the helper-class.
//
// ANCHOR_MAP_EREASEBACKGROUND() 
//                    You may call this on WM_ERASEBKGND messages to repaint.
// 
// ScreenToClient     Overrided ScreenToClient method. #declare USE_ANF_SCREEN_TO_CLIENT
//                    to enable
// ============================================================================


#define DECLARE_ANCHOR_MAP() ControlAnchorMap m_bpfxAnchorMap; \
                                void InitAnchors(DWORD dwFlags = 0); \
                                void HandleAnchors();

#define BEGIN_ANCHOR_MAP(theclass) void theclass::HandleAnchors() { \
                                m_bpfxAnchorMap.HandleAnchors(); \
                                }; \
                                void theclass::InitAnchors(DWORD dwFlags) {

#ifdef DEBUG_ANCHORLIB
#define ANCHOR_MAP_ENTRY(hWndParent, nIDCtrl, nFlags, name) m_bpfxAnchorMap.AddControl(hWndParent, nIDCtrl, nFlags, name);
#define ANCHOR_MAP_ENTRY_RANGE(hWndParent, nIDCtrlFrom, nIDCtrlTo, nFlags, rangename) \
                                for (int iCtrl=nIDCtrlFrom; iCtrl <= nIDCtrlTo; iCtrl++) \
                                std::ignore = m_bpfxAnchorMap.AddControl(hWndParent, iCtrl, nFlags, name);
#define ANCHOR_MAP_CHILDWINDOW(hWndWindow, nFlags, name) m_bpfxAnchorMap.AddChildWindow(hWndWindow, nFlags, name)
#else
#define ANCHOR_MAP_ENTRY(hWndParent, nIDCtrl, nFlags) std::ignore = m_bpfxAnchorMap.AddControl(hWndParent, nIDCtrl, nFlags);
#define ANCHOR_MAP_ENTRY_RANGE(hWndParent, nIDCtrlFrom, nIDCtrlTo, nFlags) \
                                for (int iCtrl=nIDCtrlFrom; iCtrl <= nIDCtrlTo; iCtrl++) \
                                std::ignore = m_bpfxAnchorMap.AddControl(hWndParent, iCtrl, nFlags);
#define ANCHOR_MAP_CHILDWINDOW(hWndWindow, nFlags) m_bpfxAnchorMap.AddChildWindow(hWndWindow, nFlags);
#endif

#define END_ANCHOR_MAP(hWndParent)   m_bpfxAnchorMap.Initialize(hWndParent, dwFlags); \
                         };
//m_bpfxAnchorMap.HandleAnchors(); \

#define ANCHOR_MAP_EREASEBACKGROUND() m_bpfxAnchorMap.EraseBackground(reinterpret_cast<HDC>(wParam))

#ifdef USE_ANF_SCREEN_TO_CLIENT
#define ScreenToClient(hWnd, pRECT) m_bpfxAnchorMap.ScreenToClientH(hWnd, pRECT)
#endif