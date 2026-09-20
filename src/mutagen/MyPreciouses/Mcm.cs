using BMK.Mutagen.Skyrim;
using Mutagen.Bethesda.Skyrim;

namespace MyPreciouses.Generator;

internal static class Mcm
{
    public static void AddQuest(SkyrimMod mod)
    {
        McmQuest.Add(
            mod,
            new McmQuestOptions
            {
                EditorId = "MyPreciouses_MCMQuest",
                DisplayName = "MyPreciouses",
                ConfigScriptName = "MyPreciouses_MCM",
                ModName = "MyPreciouses",
            }
        );
    }
}
