using System.IO;
using UnityEditor;
using UnityEditor.AssetImporters;
using UnityEngine;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Imports .pcg files (JSON graph data) as PcgGraphAsset so that
    /// double-click opens the Graph Editor instead of an external text editor.
    /// </summary>
    [ScriptedImporter(1, "pcg")]
    public sealed class PcgAssetImporter : ScriptedImporter
    {
        private const string ThumbnailPath = "Assets/PcgPlugin/Editor/Icons/pcg-icon-32.png";

        public override void OnImportAsset(AssetImportContext ctx)
        {
            var json = File.ReadAllText(ctx.assetPath);

            var asset = ScriptableObject.CreateInstance<PcgGraphAsset>();
            asset.SetGraphJson(json);

            var thumbnail = AssetDatabase.LoadAssetAtPath<Texture2D>(ThumbnailPath);
            if (thumbnail != null)
                ctx.AddObjectToAsset("main", asset, thumbnail);
            else
                ctx.AddObjectToAsset("main", asset);

            ctx.SetMainObject(asset);
        }
    }
}
