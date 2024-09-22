#include <windows.h>
#include <stdint.h>

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
    int BytesPerPixel;
};

// TODO: These shouldn't be global forever
global_variable bool Running;
global_variable offscreen_buffer GlobalBackBuffer;

internal_func void
RenderWeirdGradient(offscreen_buffer Buffer, int XOffset, int YOffset)
{
    // TODO: see what the optimizer does if we pass Buffer by value VS by pointer
    uint8* Row = (uint8*)Buffer.Memory;
    for (int Y = 0; Y < Buffer.Height; ++Y)
    {
        uint32* Pixel = (uint32*)Row;
        for (int X = 0; X < Buffer.Width; ++X)
        {
            /*
               Pixel:           RR GG BB xx
     Actually in memory it is:  BB GG RR xx

     And it's Little-Endian, so in the register it's:
                                xx RR GG BB
                                ↑  ↑  ↑  ↑
Padding : 0xFF << 8 * 3 sets FF ┘  |  |  |
    Red :    0xFF << 8 * 2 sets FF ┘  |  |
  Green :      0xFF << 8 * 1 sets FF  ┘  |
   Blue :         0xFF << 8 * 0 sets FF  ┘
            */
            uint8 Blue = X + XOffset;
            uint8 Green = Y + YOffset;
            uint8 Red = 0;
            uint8 Padding = 0;

            *Pixel++ = ((Padding << 8*3) |
                        (Red     << 8*2) |
                        (Green   << 8*1) |
                        (Blue    << 8*0));
        }
        Row += Buffer.Pitch;
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
    Buffer->BytesPerPixel = 4;
    
    Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
    Buffer->Info.bmiHeader.biWidth = Buffer->Width;
    Buffer->Info.bmiHeader.biHeight = -Buffer->Height; // Negative biHeight -> top-down DIB instead of bottom-up with upper-left corner as origin
    Buffer->Info.bmiHeader.biPlanes = 1;
    Buffer->Info.bmiHeader.biBitCount = 32; // TODO: Learn why we want to be DWORD-aligned (this is why 32 was chosen)
    Buffer->Info.bmiHeader.biCompression = BI_RGB;

    int BitmapMemorySize = Buffer->BytesPerPixel * Buffer->Width * Buffer->Height;
    Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);
    
    Buffer->Pitch = Buffer->Width * Buffer->BytesPerPixel;
    
    // TODO: Clear this to black
}

internal_func void
J_DisplayBufferInWindow(HDC DeviceContext, RECT ClientRect, 
                        offscreen_buffer Buffer,
                        int X, int Y, int Width, int Height)
{
    int WindowWidth = ClientRect.right - ClientRect.left;
    int WindowHeight = ClientRect.bottom - ClientRect.top;
    StretchDIBits(DeviceContext,
                  /*X, Y, Width, Height,
                  X, Y, Width, Height,*/
                  0, 0, Buffer.Width, Buffer.Height,
                  0, 0, WindowWidth, WindowHeight,
                  Buffer.Memory,
                  &(Buffer.Info),
                  DIB_RGB_COLORS,
                  SRCCOPY);
}

LRESULT CALLBACK J_MainWindowCallback(HWND Window,
    UINT Message,
    WPARAM WParam,
    LPARAM LParam)
{
    LRESULT Result = 0;
    switch (Message)
    {
    case WM_SIZE:
    {
        RECT ClientRect;
        GetClientRect(Window, &ClientRect);
        int Width = ClientRect.right - ClientRect.left;
        int Height = ClientRect.bottom - ClientRect.top;
        J_ResizeDIBSection(&GlobalBackBuffer, Width, Height);
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

    case WM_PAINT:
    {
        PAINTSTRUCT Paint;
        HDC DeviceContext = BeginPaint(Window, &Paint);
        int X = Paint.rcPaint.left;
        int Y = Paint.rcPaint.right;
        int Width = Paint.rcPaint.right - Paint.rcPaint.left;
        int Height = Paint.rcPaint.bottom - Paint.rcPaint.top;

        RECT ClientRect;
        GetClientRect(Window, &ClientRect);
        J_DisplayBufferInWindow(DeviceContext, ClientRect,
                                GlobalBackBuffer,
                                X, Y, Width, Height);
        EndPaint(Window, &Paint);
    } break;

    default:
    {
        //OutputDebugString(L"default\n");
        Result = DefWindowProc(Window, Message, WParam, LParam);
    } break;
    }
    return Result;
}

int CALLBACK
WinMain(HINSTANCE Instance,
    HINSTANCE PrevInstance,
    LPSTR CommandLine,
    int ShowCode)
{
    WNDCLASS WindowClass = {};
    WindowClass.style = CS_HREDRAW | CS_VREDRAW; // Flags to redraw whole window instead of painting only the new section when resizing
    WindowClass.lpfnWndProc = J_MainWindowCallback;
    WindowClass.hInstance = Instance;
    //WindowClass.hIcon;
    WindowClass.lpszClassName = L"JinoGameWindowClass";

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
                RenderWeirdGradient(GlobalBackBuffer, XOffset, YOffset);

                HDC DeviceContext = GetDC(Window);
                RECT ClientRect;
                GetClientRect(Window, &ClientRect);
                int WindowWidth = ClientRect.right - ClientRect.left;
                int WindowHeight = ClientRect.bottom - ClientRect.top;
                J_DisplayBufferInWindow(DeviceContext, ClientRect, 
                                        GlobalBackBuffer, 
                                        0, 0, WindowWidth, WindowHeight);
                ReleaseDC(Window, DeviceContext);

                ++XOffset;
            }
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
