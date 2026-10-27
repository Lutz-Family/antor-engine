using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using Antor.Net;
using Antor.Protocol;
using Xunit;

namespace Antor.Net.Tests;

public partial class HandshakeTests
{
    [Fact]
    public async Task Client_CompletesHandshake_AndServerExitsOnSigTerm()
    {
        var serverBin = FindServerBinary();
        var stderr = new StringBuilder();
        using var process = new Process();
        process.StartInfo = new ProcessStartInfo
        {
            FileName = serverBin,
            Arguments = "--bind 127.0.0.1 --port 0 --tick-ms 20 --shard 4",
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
        };
        process.ErrorDataReceived += (_, args) =>
        {
            if (args.Data is not null)
            {
                stderr.AppendLine(args.Data);
            }
        };

        Assert.True(process.Start(), "failed to start antor-server");
        process.BeginErrorReadLine();
        try
        {
            var line = await ReadLineAsync(process, TimeSpan.FromSeconds(5), stderr).ConfigureAwait(true);
            var parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            Assert.True(parts.Length == 3 && parts[0] == "listening", $"unexpected listening line: {line}");
            var port = int.Parse(parts[2], CultureInfo.InvariantCulture);

            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
            var connection = await ClientConnection.ConnectAndHandshakeAsync(
                "127.0.0.1",
                port,
                0xC0FFEEu,
                timeout.Token).ConfigureAwait(true);
            using (connection)
            {
                Assert.Equal(ProtocolVersion.Current, connection.Handshake.ProtocolVersion);
                Assert.Equal(0xC0FFEEu, connection.Handshake.ClientNonce);
                Assert.NotEqual(0u, connection.Handshake.ServerNonce);
            }
        }
        finally
        {
            if (!process.HasExited)
            {
                var signaled = Native.Kill(process.Id, Native.SigTerm);
                if (signaled != 0)
                {
                    process.Kill(entireProcessTree: true);
                }
            }
        }

        if (!process.WaitForExit(5000))
        {
            process.Kill(entireProcessTree: true);
            throw new TimeoutException($"antor-server did not exit after SIGTERM. stderr: {stderr}");
        }

        Assert.True(process.ExitCode == 0, $"antor-server exit {process.ExitCode}. stderr: {stderr}");
    }

    private static async Task<string> ReadLineAsync(Process process, TimeSpan timeout, StringBuilder stderr)
    {
        var read = process.StandardOutput.ReadLineAsync();
        var completed = await Task.WhenAny(read, Task.Delay(timeout)).ConfigureAwait(true);
        if (completed != read)
        {
            throw new TimeoutException($"antor-server produced no listening line. stderr: {stderr}");
        }

        var line = await read.ConfigureAwait(true);
        if (string.IsNullOrWhiteSpace(line))
        {
            throw new InvalidOperationException($"antor-server closed stdout. stderr: {stderr}");
        }

        return line;
    }

    private static string FindServerBinary()
    {
        var fromEnv = Environment.GetEnvironmentVariable("ANTOR_SERVER_BIN");
        if (!string.IsNullOrWhiteSpace(fromEnv))
        {
            if (!File.Exists(fromEnv))
            {
                throw new FileNotFoundException("ANTOR_SERVER_BIN does not exist.", fromEnv);
            }

            return fromEnv;
        }

        foreach (var start in new[] { AppContext.BaseDirectory, Directory.GetCurrentDirectory() })
        {
            var dir = new DirectoryInfo(start);
            while (dir is not null)
            {
                foreach (var preset in new[] { "sanitizer", "debug", "release" })
                {
                    var candidate = Path.Combine(dir.FullName, "build", preset, "antor-server");
                    if (File.Exists(candidate))
                    {
                        return candidate;
                    }
                }

                dir = dir.Parent;
            }
        }

        throw new FileNotFoundException(
            "Set ANTOR_SERVER_BIN to the antor-server executable, or build a CMake preset first.");
    }

    private static partial class Native
    {
        internal const int SigTerm = 15;

        [LibraryImport("libc", EntryPoint = "kill", SetLastError = true)]
        internal static partial int Kill(int pid, int signal);
    }
}
