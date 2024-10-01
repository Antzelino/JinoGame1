#include <windows.h>
#include <stdint.h>
#include <xinput.h>

#define internal_func static
#define local_persist static
#define global_variable static

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

struct offscreen_buffer
{
    BITMAPINFO Info;
    void* Memory;
    int Width;
    int Height;
    int Pitch;
};

struct window_dimensions
{
    int Width;
    int Height;
};

// TODO: These shouldn't be global forever
global_variable bool Running;
global_variable offscreen_buffer GlobalBackBuffer;

// Use '#define' for the functions so we can have stub functions, in case the DLL for XInput cannot load in a system due to Platform Requirements (https://learn.microsoft.com/en-us/windows/win32/api/xinput/nf-xinput-xinputgetstate#platform-requirements)
#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE *pState)
#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
// This is to be able to have pointers to the functions
typedef X_INPUT_GET_STATE(x_input_get_state);
typedef X_INPUT_SET_STATE(x_input_set_state);
// Defining stubs for the functions which do nothing. Now we can initialize the function pointers with these and prevent Access Violation Exceptions
internal_func X_INPUT_GET_STATE(XInputGetStateStub) { return 0; }
internal_func X_INPUT_SET_STATE(XInputSetStateStub) { return 0; }
// Creating the pointers to the functions, with a similar name to the one in xinput.h and initially we set those to point to the stub functions
global_variable x_input_get_state* XInputGetState_ = XInputGetStateStub;
global_variable x_input_set_state* XInputSetState_ = XInputSetStateStub;
// With this we can use the xinput.h names of the functions
#define XInputGetState XInputGetState_
#define XInputSetState XInputSetState_

internal_func void
J_LoadXInput()
{
    // TODO: There is a XINPUT_DLL macro defined in xinput.h which now is "xinput1_4.dll", later I should check if that's better
    HMODULE XInputLibrary = LoadLibrary(L"xinput1_3.dll");
    if (XInputLibrary)
    {
        XInputGetState = (x_input_get_state*)GetProcAddress(XInputLibrary, "XInputGetState");
        XInputSetState = (x_input_set_state *)GetProcAddress(XInputLibrary, "XInputSetState");
    }
}

internal_func window_dimensions
J_GetWindowDimensions(HWND Window)
{
    window_dimensions Result = {};

    RECT ClientRect;
    GetClientRect(Window, &ClientRect);
    Result.Width = ClientRect.right - ClientRect.left;
    Result.Height = ClientRect.bottom - ClientRect.top;

    return Result;
}

internal_func void
RenderWeirdGradient(offscreen_buffer *Buffer, int XOffset, int YOffset)
{
    // TODO: See what the optimizer does if we pass Buffer by value VS by pointer
    uint8* Row = (uint8*)Buffer->Memory;
    for (int Y = 0; Y < Buffer->Height; ++Y)
    {
        uint32* Pixel = (uint32*)Row;
        for (int X = 0; X < Buffer->Width; ++X)
        {
            /*
               Pixel:           RR GG BB xx
     Actually in memory it is:  BB GG RR xx           <- Because Microsoft decided to do it that way

     And it's Little-Endian, so in the register it's:
                                xx RR GG BB           <- Probably this is why they decided that. So in Little-Endian they can have order RGB
                                ↑  ↑  ↑  ↑
Padding : 0xFF << 8 * 3 sets FF ┘  |  |  |
    Red :    0xFF << 8 * 2 sets FF ┘  |  |
  Green :      0xFF << 8 * 1 sets FF  ┘  |
   Blue :         0xFF << 8 * 0 sets FF  ┘
            */
            uint8 Blue = X + XOffset;
            uint8 Green = Y + YOffset;
            uint8 Red = (XOffset + YOffset)/2;
            uint8 Padding = 0;

            *Pixel++ = ((Padding << 8*3) |
                        (Red     << 8*2) |
                        (Green   << 8*1) |
                        (Blue    << 8*0));
        }
        Row += Buffer->Pitch;
    }
}

internal_func void
J_ResizeDIBSection(offscreen_buffer *Buffer, int Width, int Height)
{
    if (Buffer->Memory)
    {
        VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
    }
    
    Buffer->Width = Width;
    Buffer->Height = Height;
    int BytesPerPixel = 4;
    
    Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
    Buffer->Info.bmiHeader.biWidth = Buffer->Width;
    Buffer->Info.bmiHeader.biHeight = -Buffer->Height; // Negative biHeight -> top-down DIB instead of bottom-up with upper-left corner as origin
    Buffer->Info.bmiHeader.biPlanes = 1;
    Buffer->Info.bmiHeader.biBitCount = 32; // TODO: Learn why we want to be DWORD-aligned (this is why 32 was chosen)
    Buffer->Info.bmiHeader.biCompression = BI_RGB;

    int BitmapMemorySize = BytesPerPixel * Buffer->Width * Buffer->Height;
    Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);
    
    Buffer->Pitch = Buffer->Width * BytesPerPixel;
    
    // TODO: Clear this to black?
}

internal_func void
J_DisplayBufferInWindow(offscreen_buffer* Buffer, HDC DeviceContext, 
                        int WindowWidth, int WindowHeight)
{
    // TODO: Aspect ratio correction
    // TODO: Play with stretch modes
    StretchDIBits(DeviceContext,
                  0, 0, WindowWidth, WindowHeight,
                  0, 0, Buffer->Width, Buffer->Height,
                  Buffer->Memory,
                  &(Buffer->Info),
                  DIB_RGB_COLORS,
                  SRCCOPY);
}

internal_func LRESULT CALLBACK
J_MainWindowCallback(HWND Window,
    UINT Message,
    WPARAM WParam,
    LPARAM LParam)
{
    LRESULT Result = 0;
    switch (Message)
    {
    case WM_SIZE:
    {
    } break;

    case WM_CLOSE:
    {
        Running = false;
    } break;

    case WM_ACTIVATEAPP:
    {
        OutputDebugString(L"WM_ACTIVATEAPP\n");
    } break;

    case WM_DESTROY:
    {
        Running = false;
    } break;

    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_KEYDOWN:
    case WM_KEYUP:
    {
        uint64 VKCode = WParam;
        bool WasDown = ((LParam & (1LL << 30)) != 0);
        bool IsDown = ((LParam & (1LL << 31)) == 0);
        if (WasDown != IsDown) // VIDEO https://youtu.be/J3y1x54vyIQ?t=3493 58:10
        {
            if (VKCode == 'W')
            {
            }
            else if (VKCode == 'A')
            {
            }
            else if (VKCode == 'S')
            {
            }
            else if (VKCode == 'D')
            {
            }
            else if (VKCode == 'Q')
            {
            }
            else if (VKCode == 'E')
            {
            }
            else if (VKCode == VK_UP)
            {
            }
            else if (VKCode == VK_DOWN)
            {
            }
            else if (VKCode == VK_LEFT)
            {
            }
            else if (VKCode == VK_RIGHT)
            {
            }
            else if (VKCode == VK_ESCAPE)
            {
                OutputDebugString(L"ESCAPE: ");
                if (IsDown)
                {
                    OutputDebugString(L"IsDown ");
                }
                if (WasDown)
                {
                    OutputDebugString(L"WasDown ");
                }
                OutputDebugString(L"\n");
            }
            else if (VKCode == VK_SPACE)
            {
            }
        }
    } break;

    case WM_PAINT:
    {
        PAINTSTRUCT Paint;
        HDC DeviceContext = BeginPaint(Window, &Paint);
        int Width = Paint.rcPaint.right - Paint.rcPaint.left;
        int Height = Paint.rcPaint.bottom - Paint.rcPaint.top;

        J_DisplayBufferInWindow(&GlobalBackBuffer, DeviceContext, 
                                Width, Height);
        EndPaint(Window, &Paint);
    } break;

    default:
    {
        Result = DefWindowProc(Window, Message, WParam, LParam); // Let Windows handle with default behavior
    } break;
    }
    return Result;
}

_Use_decl_annotations_
int CALLBACK
WinMain(HINSTANCE Instance,
    HINSTANCE PrevInstance,
    LPSTR CommandLine,
    int ShowCode)
{
    J_LoadXInput();
    WNDCLASS WindowClass = {};
    WindowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC; // Flags to redraw whole window instead of painting only the new section when resizing
    WindowClass.lpfnWndProc = J_MainWindowCallback;
    WindowClass.hInstance = Instance;
    //WindowClass.hIcon;
    WindowClass.lpszClassName = L"JinoGameWindowClass";
    J_ResizeDIBSection(&GlobalBackBuffer, 1366, 768);

    if (RegisterClass(&WindowClass))
    {
        HWND Window = CreateWindowEx(0,
            WindowClass.lpszClassName,
            L"Jino Game",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            0,
            0,
            Instance,
            0);
        if (Window)
        {
            HDC DeviceContext = GetDC(Window);
            int XOffset = 0;
            int YOffset = 0;

            Running = true;
            while (Running)
            {
                MSG Message;
                while (PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
                {
                    if (Message.message == WM_QUIT)
                    {
                        Running = false;
                    }
                    TranslateMessage(&Message);
                    DispatchMessage(&Message);
                }

                // TODO: Check if we should poll more frequently for input
                for (DWORD controllerIndex = 0; controllerIndex < XUSER_MAX_COUNT; controllerIndex++)
                {
                    XINPUT_STATE ControllerState;
                    if (XInputGetState(controllerIndex, &ControllerState) == ERROR_SUCCESS)
                    {
                        // Controller is plugged in
                        // TODO: See if ControllerState.dwPacketNumber increments too rapidly
                        XINPUT_GAMEPAD* Pad = &ControllerState.Gamepad;
                        
                        // Buttons
                        bool Up = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP);
                        bool Down = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
                        bool Left = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
                        bool Right = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
                        bool Start = (Pad->wButtons & XINPUT_GAMEPAD_START);
                        bool Back = (Pad->wButtons & XINPUT_GAMEPAD_BACK);
                        bool LeftThumb = (Pad->wButtons & XINPUT_GAMEPAD_LEFT_THUMB);
                        bool RightThumb = (Pad->wButtons & XINPUT_GAMEPAD_RIGHT_THUMB);
                        bool LeftShoulder = (Pad->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
                        bool RightShoulder = (Pad->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
                        bool A = (Pad->wButtons & XINPUT_GAMEPAD_A);
                        bool B = (Pad->wButtons & XINPUT_GAMEPAD_B);
                        bool X = (Pad->wButtons & XINPUT_GAMEPAD_X);
                        bool Y = (Pad->wButtons & XINPUT_GAMEPAD_Y);

                        // Triggers [0 , 255]
                        BYTE LeftTrigger = Pad->bLeftTrigger;
                        BYTE RightTrigger = Pad->bRightTrigger;

                        // ThumbSticks [-32768 , 32767]
                        SHORT LeftStickX = Pad->sThumbLX;
                        SHORT LeftStickY = -Pad->sThumbLY;
                        SHORT RightStickX = Pad->sThumbRX;
                        SHORT RightStickY = Pad->sThumbRY;

                        XOffset += LeftStickX >> 12;
                        YOffset += LeftStickY >> 12;
                    }
                    else
                    {
                        // Controller is not available
                    }
                }

                RenderWeirdGradient(&GlobalBackBuffer, XOffset, YOffset);

                window_dimensions Dimensions = J_GetWindowDimensions(Window);
                J_DisplayBufferInWindow(&GlobalBackBuffer, DeviceContext,
                                        Dimensions.Width, Dimensions.Height);
            }
            ReleaseDC(Window, DeviceContext);
        }
        else
        {
            // TODO: Error logging here
        }
    }
    else
    {
        // TODO: Error logging here
    }
    return 0;
}
