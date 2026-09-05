using System.Diagnostics;
using System.Runtime.InteropServices;

namespace WinMix.Audio;

/// <summary>A process's user-facing identity, as far as we could determine it.</summary>
/// <param name="Pid">The owning process id.</param>
/// <param name="FriendlyName">Best available display name, never empty.</param>
/// <param name="ExecutablePath">Full path to the image, or null if we could not read it.</param>
public sealed record ProcessInfo(uint Pid, string FriendlyName, string? ExecutablePath);

/// <summary>
/// Resolves process ids to display names and executable paths, caching the result.
///
/// Resolution deliberately uses <c>QueryFullProcessImageName</c> rather than
/// <see cref="Process.MainModule"/>: MainModule needs PROCESS_VM_READ, which is
/// denied for anything running at a higher elevation than us, so a plain
/// user-level build would show blanks for a surprising number of real apps.
/// PROCESS_QUERY_LIMITED_INFORMATION succeeds far more often.
/// </summary>
public sealed partial class ProcessInfoCache
{
    private const uint ProcessQueryLimitedInformation = 0x1000;

    private readonly Dictionary<uint, ProcessInfo> _cache = new();

    public ProcessInfo Get(uint pid)
    {
        if (_cache.TryGetValue(pid, out var cached))
        {
            return cached;
        }

        var resolved = Resolve(pid);
        _cache[pid] = resolved;
        return resolved;
    }

    /// <summary>
    /// Drops cache entries for processes that are no longer playing audio. Windows
    /// recycles pids, so an unbounded cache would eventually mislabel a session.
    /// </summary>
    public void Trim(IReadOnlySet<uint> livePids)
    {
        if (_cache.Count == 0)
        {
            return;
        }

        var stale = _cache.Keys.Where(pid => !livePids.Contains(pid)).ToList();
        foreach (var pid in stale)
        {
            _cache.Remove(pid);
        }
    }

    private static ProcessInfo Resolve(uint pid)
    {
        if (pid == 0)
        {
            return new ProcessInfo(pid, "System sounds", null);
        }

        var path = TryGetExecutablePath(pid);
        if (path is null)
        {
            return new ProcessInfo(pid, TryGetProcessName(pid) ?? $"PID {pid}", null);
        }

        return new ProcessInfo(pid, DescribeImage(path), path);
    }

    /// <summary>
    /// Prefers the executable's FileDescription ("Spotify") over its file name
    /// ("spotify"), matching what the Windows mixer shows.
    /// </summary>
    private static string DescribeImage(string path)
    {
        try
        {
            var description = FileVersionInfo.GetVersionInfo(path).FileDescription;
            if (!string.IsNullOrWhiteSpace(description))
            {
                return description.Trim();
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // Fall through to the file name.
        }

        var fileName = Path.GetFileNameWithoutExtension(path);
        return string.IsNullOrWhiteSpace(fileName) ? path : fileName;
    }

    private static string? TryGetProcessName(uint pid)
    {
        try
        {
            using var process = Process.GetProcessById((int)pid);
            return process.ProcessName;
        }
        catch (Exception ex) when (ex is ArgumentException or InvalidOperationException)
        {
            // The process exited between enumeration and now.
            return null;
        }
    }

    private static string? TryGetExecutablePath(uint pid)
    {
        var handle = OpenProcess(ProcessQueryLimitedInformation, false, pid);
        if (handle == IntPtr.Zero)
        {
            return null;
        }

        try
        {
            unsafe
            {
                const int capacity = 1024;
                var buffer = stackalloc char[capacity];
                var size = (uint)capacity;

                // On success Windows rewrites size to the character count written.
                return QueryFullProcessImageName(handle, 0, buffer, &size)
                    ? new string(buffer, 0, (int)size)
                    : null;
            }
        }
        finally
        {
            CloseHandle(handle);
        }
    }

    [LibraryImport("kernel32.dll", SetLastError = true)]
    private static partial IntPtr OpenProcess(
        uint dwDesiredAccess,
        [MarshalAs(UnmanagedType.Bool)] bool bInheritHandle,
        uint dwProcessId);

    // Declared with raw pointers rather than char[]/ref: the source-generated
    // marshaller refuses array and by-ref primitives unless the whole assembly
    // opts out of runtime marshalling, and pointers are already blittable.
    [LibraryImport("kernel32.dll", EntryPoint = "QueryFullProcessImageNameW", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static unsafe partial bool QueryFullProcessImageName(
        IntPtr hProcess,
        uint dwFlags,
        char* lpExeName,
        uint* lpdwSize);

    [LibraryImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool CloseHandle(IntPtr hObject);
}
