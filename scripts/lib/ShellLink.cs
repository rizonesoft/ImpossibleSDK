// Windows shortcuts (.lnk) written and read through the shell's own IShellLinkW, which keeps every field in Unicode at
// its full length. WScript.Shell goes through the ANSI code page (a CJK name cannot be saved, and its text is stored as
// '?') and Shell.Application reads arguments cut at 259 characters (both probed 2026-11-18, D07 T01 section 7).
// PowerShell loads it with Add-Type -Path; it needs nothing beyond .NET on Windows.
using System;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;

namespace Conclave
{
    [ComImport, Guid("000214F9-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IShellLinkW
    {
        void GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder file, int size, IntPtr data, uint flags);
        void GetIDList(out IntPtr list);
        void SetIDList(IntPtr list);
        void GetDescription([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder name, int size);
        void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string name);
        void GetWorkingDirectory([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder dir, int size);
        void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string dir);
        void GetArguments([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder args, int size);
        void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string args);
        void GetHotkey(out short key);
        void SetHotkey(short key);
        void GetShowCmd(out int show);
        void SetShowCmd(int show);
        void GetIconLocation([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder path, int size, out int index);
        void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string path, int index);
        void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string path, uint reserved);
        void Resolve(IntPtr window, uint flags);
        void SetPath([MarshalAs(UnmanagedType.LPWStr)] string file);
    }

    [ComImport, Guid("00021401-0000-0000-C000-000000000046")]
    internal class CShellLink
    {
    }

    public sealed class ShellLinkData
    {
        public string Target = "";
        public string Arguments = "";
        public string WorkingDirectory = "";
        public string Description = "";
        public string IconPath = "";
        public int IconIndex;
    }

    public static class ShellPath
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern int GetLongPathNameW(string shortPath, StringBuilder longPath, int size);

        // The long spelling of an existing path (C:\Users\RUNNER~1 is C:\Users\runneradmin), so one folder has one
        // name whichever spelling it was handed in; a path that does not exist, or that the call cannot expand, is
        // returned as it came.
        public static string Long(string path)
        {
            var text = new StringBuilder(32768);
            int length = GetLongPathNameW(path, text, text.Capacity);
            return length > 0 && length < text.Capacity ? text.ToString() : path;
        }
    }

    public static class ShellLink
    {
        private const int Size = 32768;  // the longest a field can be
        private const uint RawPath = 4;  // SLGP_RAWPATH: the target as written, environment variables unexpanded

        // Writes `path` (any name), replacing a file there.
        public static void Write(string path, ShellLinkData data)
        {
            var link = (IShellLinkW)new CShellLink();
            link.SetPath(data.Target);
            link.SetArguments(data.Arguments);
            link.SetWorkingDirectory(data.WorkingDirectory);
            link.SetDescription(data.Description);
            link.SetIconLocation(data.IconPath, data.IconIndex);
            ((IPersistFile)link).Save(path, true);
        }

        // Reads `path`; raises when it is not a shortcut the shell can load.
        public static ShellLinkData Read(string path)
        {
            var link = (IShellLinkW)new CShellLink();
            ((IPersistFile)link).Load(path, 0);
            var text = new StringBuilder(Size);
            var data = new ShellLinkData();
            link.GetPath(text, Size, IntPtr.Zero, RawPath);
            data.Target = text.ToString();
            link.GetArguments(text.Clear(), Size);
            data.Arguments = text.ToString();
            link.GetWorkingDirectory(text.Clear(), Size);
            data.WorkingDirectory = text.ToString();
            link.GetDescription(text.Clear(), Size);
            data.Description = text.ToString();
            link.GetIconLocation(text.Clear(), Size, out data.IconIndex);
            data.IconPath = text.ToString();
            return data;
        }
    }
}
