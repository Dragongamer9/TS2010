# Material kinds: what the texture alpha means (documentation findings)

Research by Perplexity (Command Center teammate), 2026-11-12; saved and checked against
the disc by Claude. The pages are unversioned legacy documentation on the Trainz wiki, not
TS2010 build specifications, and they give texture roles, not the `MATL` slot numbers.

| Kind | Diffuse alpha (quoted) | Source |
|---|---|---|
| `m.onetex` | "Diffuse map - RGB = diffuse color, A = opacity" | [M.onetex](https://online.ts2009.com/mediaWiki/index.php/M.onetex) |
| `m.reflect` | "Diffuse map - A = Per-pixel opacity, multiplied by the material-wide opacity setting. Black = fully transparent. White = fully opaque." (the page's introduction also says "The opacity of the material is defined by the material-wide opacity setting") | [M.reflect](https://online.ts2009.com/mediaWiki/index.php/M.reflect) |
| `m.gloss` | "Diffuse map - A = Reflection strength/mask. black = no reflection. White = full reflection." | [M.gloss](https://online.ts2009.com/mediaWiki/index.php/M.gloss) |
| `m.tbumptex` | "Diffuse map - RGB = diffuse color, A = opacity"; the normal map's alpha is specular strength | [M.tbumptex](https://online.ts2009.com/mediaWiki/index.php/M.tbumptex), [CCG bump mapping](https://online.ts2009.com/mediaWiki/index.php/CCG/Modelling:Bump_Mapping_Information) ("TS2009 onwards") |
| `m.tbumpgloss` | "Diffuse map - RGB = diffuse color, A = reflection strength/mask" (the page notes that in TRS2019 build 105175 the diffuse and normal alphas appear swapped; a later version) | [M.tbumpgloss](https://online.ts2009.com/mediaWiki/index.php/M.tbumpgloss) |
| `m.notex` | no textures: "Material settings control diffuse and specular color and opacity." | [M.notex](https://online.ts2009.com/mediaWiki/index.php/M.notex) |

**m.reflect in practice:** the TRS2009 outline
[Modeling Trains for Trainz](https://wiki.trainzsimulator.com/wiki/Modeling_Trains_for_Trainz)
sets up windows as `m.reflect` with "Alpha blending is set using the Opacity spinner rather
than by alpha channel" and a "128x128 24-bit tga" diffuse map. The legacy Blender exporter
guide maps "diffuse & alpha & reflection -> m.gloss", so adding alpha to reflective glass made
it `m.gloss`. No TS2009/TS2010 post was found showing `m.reflect` diffuse alpha cutting holes.

**Checked on the disc (Claude):** all 3,897 distinct `m.reflect` colour maps are fully
solid (3,893) or have smooth alpha (4); none looks like a cut-out. Of 401 sampled `m.gloss`
colour maps, 277 have smooth alpha and 113 would pass the cut-out rule, which fits the
"reflection mask" reading.

**Port:** `materialMayCutOut` returns false for `m.gloss` and `m.tbumpgloss` only.
