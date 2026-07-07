using UnityEngine;
#if UNITY_EDITOR
using UnityEditor;
#endif

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Global PCG settings stored as a ScriptableObject asset.
    /// </summary>
    public sealed class PcgProjectSettings : ScriptableObject
    {
        [SerializeField] private bool enableLogging = false;
        [SerializeField] private PcgScatterDisplayMode defaultScatterDisplayMode = PcgScatterDisplayMode.MergedMesh;

        public bool EnableLogging => enableLogging;

        private const string AssetPath = "Assets/ProjectSettings/PcgProjectSettings.asset";
        private static PcgProjectSettings s_Instance;

        public static bool IsLogEnabled
        {
            get
            {
#if UNITY_EDITOR
                return GetOrCreateInstance().enableLogging;
#else
                return false;
#endif
            }
        }

        public static PcgScatterDisplayMode DefaultScatterDisplayMode
        {
            get
            {
#if UNITY_EDITOR
                return GetOrCreateInstance().defaultScatterDisplayMode;
#else
                return PcgScatterDisplayMode.MergedMesh;
#endif
            }
        }

#if UNITY_EDITOR
        private static PcgProjectSettings GetOrCreateInstance()
        {
            if (s_Instance != null)
                return s_Instance;

            s_Instance = AssetDatabase.LoadAssetAtPath<PcgProjectSettings>(AssetPath);
            if (s_Instance == null)
            {
                if (!AssetDatabase.IsValidFolder("Assets/ProjectSettings"))
                    AssetDatabase.CreateFolder("Assets", "ProjectSettings");
                s_Instance = CreateInstance<PcgProjectSettings>();
                AssetDatabase.CreateAsset(s_Instance, AssetPath);
                AssetDatabase.SaveAssets();
            }

            return s_Instance;
        }

        public static void SetLogging(bool enabled)
        {
            var settings = GetOrCreateInstance();
            settings.enableLogging = enabled;
            EditorUtility.SetDirty(settings);
            AssetDatabase.SaveAssets();
        }

        public static void SetDefaultScatterDisplayMode(PcgScatterDisplayMode mode)
        {
            var settings = GetOrCreateInstance();
            settings.defaultScatterDisplayMode = mode;
            EditorUtility.SetDirty(settings);
            AssetDatabase.SaveAssets();
        }
#endif
    }
}
