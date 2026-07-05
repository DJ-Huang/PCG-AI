using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace DJTechRuntime.PCG
{
    /// <summary>Minimal JSON parser and serializer (no external deps).</summary>
    public static class PcgMiniJson
    {
        public static object Deserialize(string json) => Parser.Parse(json);

        public static string Serialize(object value)
        {
            var sb = new StringBuilder(256);
            WriteValue(sb, value);
            return sb.ToString();
        }

        private static void WriteValue(StringBuilder sb, object value)
        {
            switch (value)
            {
                case null:
                    sb.Append("null");
                    break;
                case bool b:
                    sb.Append(b ? "true" : "false");
                    break;
                case int i:
                    sb.Append(i.ToString(CultureInfo.InvariantCulture));
                    break;
                case long l:
                    sb.Append(l.ToString(CultureInfo.InvariantCulture));
                    break;
                case float f:
                    sb.Append(f.ToString(CultureInfo.InvariantCulture));
                    break;
                case double d:
                    sb.Append(d.ToString(CultureInfo.InvariantCulture));
                    break;
                case string s:
                    WriteString(sb, s);
                    break;
                case Dictionary<string, object> dict:
                    WriteObject(sb, dict);
                    break;
                case List<object> list:
                    WriteArray(sb, list);
                    break;
                default:
                    WriteString(sb, value?.ToString() ?? "");
                    break;
            }
        }

        private static void WriteObject(StringBuilder sb, Dictionary<string, object> dict)
        {
            sb.Append('{');
            var first = true;
            foreach (var (key, val) in dict)
            {
                if (!first) sb.Append(", ");
                first = false;
                WriteString(sb, key);
                sb.Append(": ");
                WriteValue(sb, val);
            }
            sb.Append('}');
        }

        private static void WriteArray(StringBuilder sb, List<object> list)
        {
            sb.Append('[');
            for (var i = 0; i < list.Count; i++)
            {
                if (i > 0) sb.Append(", ");
                WriteValue(sb, list[i]);
            }
            sb.Append(']');
        }

        private static void WriteString(StringBuilder sb, string value)
        {
            sb.Append('"');
            foreach (var c in value)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default: sb.Append(c); break;
                }
            }
            sb.Append('"');
        }

        private sealed class Parser
        {
            private readonly string _json;
            private int _index;

            private Parser(string json) => _json = json;

            public static object Parse(string json) => new Parser(json).ParseValue();

            private void SkipWhitespace()
            {
                while (_index < _json.Length && char.IsWhiteSpace(_json[_index]))
                    _index++;
            }

            private char Peek() => _json[_index];
            private char Next() => _json[_index++];

            private object ParseValue()
            {
                SkipWhitespace();
                if (_index >= _json.Length)
                    return null;

                return Peek() switch
                {
                    '{' => ParseObject(),
                    '[' => ParseArray(),
                    '"' => ParseString(),
                    't' or 'f' => ParseBool(),
                    'n' => ParseNull(),
                    _ => ParseNumber(),
                };
            }

            private Dictionary<string, object> ParseObject()
            {
                var dict = new Dictionary<string, object>();
                Next();
                SkipWhitespace();
                if (Peek() == '}')
                {
                    Next();
                    return dict;
                }

                while (true)
                {
                    SkipWhitespace();
                    var key = ParseString();
                    SkipWhitespace();
                    if (Next() != ':')
                        throw new FormatException("Expected ':' in object.");

                    dict[key] = ParseValue();
                    SkipWhitespace();
                    if (Peek() == '}')
                    {
                        Next();
                        break;
                    }

                    if (Next() != ',')
                        throw new FormatException("Expected ',' in object.");
                }

                return dict;
            }

            private List<object> ParseArray()
            {
                var list = new List<object>();
                Next();
                SkipWhitespace();
                if (Peek() == ']')
                {
                    Next();
                    return list;
                }

                while (true)
                {
                    list.Add(ParseValue());
                    SkipWhitespace();
                    if (Peek() == ']')
                    {
                        Next();
                        break;
                    }

                    if (Next() != ',')
                        throw new FormatException("Expected ',' in array.");
                }

                return list;
            }

            private string ParseString()
            {
                if (Next() != '"')
                    throw new FormatException("Expected string.");

                var sb = new StringBuilder();
                while (_index < _json.Length)
                {
                    var c = Next();
                    if (c == '"')
                        return sb.ToString();

                    if (c == '\\')
                    {
                        var esc = Next();
                        switch (esc)
                        {
                            case '"': sb.Append('"'); break;
                            case '\\': sb.Append('\\'); break;
                            case 'n': sb.Append('\n'); break;
                            case 't': sb.Append('\t'); break;
                            default: sb.Append(esc); break;
                        }
                    }
                    else sb.Append(c);
                }

                throw new FormatException("Unterminated string.");
            }

            private object ParseNumber()
            {
                var start = _index;
                if (Peek() == '-')
                    _index++;

                while (_index < _json.Length && (char.IsDigit(_json[_index]) || _json[_index] == '.'))
                    _index++;

                var text = _json.Substring(start, _index - start);
                if (text.Contains('.'))
                    return double.Parse(text, CultureInfo.InvariantCulture);
                return long.Parse(text, CultureInfo.InvariantCulture);
            }

            private object ParseBool()
            {
                if (_json.Substring(_index, 4) == "true") { _index += 4; return true; }
                if (_json.Substring(_index, 5) == "false") { _index += 5; return false; }
                throw new FormatException("Invalid boolean.");
            }

            private object ParseNull()
            {
                if (_json.Substring(_index, 4) != "null")
                    throw new FormatException("Invalid null.");
                _index += 4;
                return null;
            }
        }
    }
}
