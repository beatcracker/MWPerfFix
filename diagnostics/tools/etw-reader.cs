// Compiled project-local ETW reader and hex conversion. No full-trace XML export.
// EventRecord owns native handles; dispose each before reading the next record.
using System;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Diagnostics.Eventing.Reader;

public static class MwPerfEtwReader
{
    private static readonly Regex Payload = new Regex(
        @"<EventPayload>\s*([0-9a-fA-F\s]+?)\s*</EventPayload>",
        RegexOptions.Compiled | RegexOptions.CultureInvariant);

    private static int Nibble(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw new FormatException("Nonhex ETW payload");
    }

    public static byte[] DecodeHex(string hex)
    {
        if (hex == null || hex.Length == 0 || (hex.Length & 1) != 0)
            throw new FormatException("Malformed ETW payload hex");
        byte[] bytes = new byte[hex.Length / 2];
        for (int i = 0; i < bytes.Length; ++i)
            bytes[i] = checked((byte)((Nibble(hex[2 * i]) << 4) | Nibble(hex[2 * i + 1])));
        return bytes;
    }

    public static long Extract(string etlPath, string outputPath)
    {
        // Filter within the Windows event reader: unrelated providers are never
        // expanded or materialized. ETL remains the authoritative input.
        const string filter =
            "*[System[Provider[@Guid='{9e8a7dcb-4fa5-45e2-a126-4d962b7ea3f1}']]]";
        long count = 0;
        var query = new EventLogQuery(etlPath, PathType.FilePath, filter);
        query.ReverseDirection = false;
        using (var reader = new EventLogReader(query))
        using (var writer = new StreamWriter(outputPath, false, new UTF8Encoding(false)))
        {
            EventRecord next;
            while ((next = reader.ReadEvent()) != null)
            {
                using (next)
                {
                    string xml = next.ToXml();
                    Match match = Payload.Match(xml);
                    int process = next.ProcessId ?? 0;
                    if (process <= 0)
                        throw new InvalidDataException("Invalid ETW process ID at event " + count);
                    writer.Write(process);
                    writer.Write('\t');
                    if (match.Success)
                    {
                        // The provider can be rendered as unformatted EventPayload.
                        // Convert and validate hex in compiled code, never a PS byte loop.
                        string hex = Regex.Replace(match.Groups[1].Value, @"\s+", "");
                        DecodeHex(hex);
                        writer.Write('R');
                        writer.WriteLine(hex);
                    }
                    else if (xml.IndexOf("<Data Name=", StringComparison.OrdinalIgnoreCase) >= 0)
                    {
                        // A system with provider metadata may render named ETW
                        // fields instead. Preserve the individual filtered event,
                        // not the entire trace, for strict named-field decoding.
                        writer.Write('N');
                        writer.WriteLine(Convert.ToBase64String(Encoding.UTF8.GetBytes(xml)));
                    }
                    else
                    {
                        throw new InvalidDataException("ETW record has no decodable payload at event " + count);
                    }
                    ++count;
                    if (count % 10000 == 0)
                        Console.Error.WriteLine("ETW provider events extracted: " + count);
                }
            }
        }
        if (count == 0) throw new InvalidDataException("No MWPerfFix provider events");
        return count;
    }
}
