// Bedrock 26.60 UI atlas generated without resampling from the owner's PNG archive.
// Packed: 16 UI sprites in a 128x128 PNG; decoded on the Android GPU render thread.
// Reproducible using scripts/generate-bedrock-atlas.py with the supplied levi_packs.zip.
#pragma once
enum class OptSprite : unsigned {
    ButtonNormal,
    ButtonHover,
    ButtonPressed,
    ButtonHoverPressed,
    ButtonDisabled,
    PanelDark,
    PanelLight,
    PanelOutline,
    Input,
    InputHover,
    ScrollHandle,
    ScrollRail,
    CheckOff,
    CheckOn,
    CheckOffHover,
    CheckOnHover,
    Count
};
struct OptSpriteRect {int x; int y; int w; int h; int corner;};
static constexpr int kOptAtlasWidth=128, kOptAtlasHeight=128;
static constexpr OptSpriteRect kOptAtlasRects[]={
    {3, 3, 7, 7, 2}, // ButtonNormal
    {33, 3, 7, 7, 2}, // ButtonHover
    {63, 3, 7, 7, 2}, // ButtonPressed
    {93, 3, 7, 7, 2}, // ButtonHoverPressed
    {3, 33, 3, 3, 1}, // ButtonDisabled
    {33, 33, 6, 6, 2}, // PanelDark
    {63, 33, 16, 16, 4}, // PanelLight
    {93, 33, 4, 4, 1}, // PanelOutline
    {3, 63, 6, 6, 2}, // Input
    {33, 63, 6, 6, 2}, // InputHover
    {63, 63, 3, 5, 1}, // ScrollHandle
    {93, 63, 1, 1, 0}, // ScrollRail
    {3, 93, 16, 13, 0}, // CheckOff
    {33, 93, 16, 13, 0}, // CheckOn
    {63, 93, 16, 13, 0}, // CheckOffHover
    {93, 93, 16, 13, 0}, // CheckOnHover
};
static constexpr char kOptAtlasPngBase64[]=
"iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAYAAADDPmHLAAAD7UlEQVR42u3dv0/UYBjA8bfmcotx0GICkuDidMadVhnkBOJwuBBlYOIPILeaYHIJJKyEsLAQw2AIJCTCIv5iAFv+B40xrN7pYFgYeB3wsL3rea30fVva7ydp0pY7nj7v+9zbH/CCEACAnDK8G6Zpyk4vbDQahqqDIK6euEEKrTvq9bpv23VdUalUlB/Il6/+uJ+/CfF4uEd53JHyiG97pjqjJd9n0y9921OTY6Iy2qd9BCgE7XRd93x9aXFJ28F8/vZ3fe3VG21xZ6oz5+uWZWmLOzU5dr5+53Yyp4ArnAXzjQLIuULQ8K9z2PcO/zqHfe/wr3PY9w7/SQ373AVwFwAAAAAAAAAAAAAAAAAAAAAAyCDTNGVz0RxadliSiht2uXDcgYEBmWSft00Nm56eTuRARh+N+hat1Sdl6MVxnLjCGqJlYk4S2qaGra6uJnIgvbd6E4krZbQPoGVZwnEcYdt2JkZ9XwHkfVqSd1p8JzHOm5SpK4AkTUxM+LbX1tZS1/kxOv+gHR0dUQBCCDE+Ps5VeI4LgBmxabkLAAUACgAUACgAUADwSOKPZ+XmOUBSXNcVlmVlsnMZAUKwbTvyU8D1jfXM5J/3BzCS9gMAAAAAAAAAAACQD4nM0pUdqI5rmtdlc9HZyOVyWaalw1t/ni1bZ+a+ff826HWxF8DYyJhv3+67XWEYhqG6AOr1H6Kn54ZoNH7m8mf7bb8SltQs3aTiap4TmDq5/5Wwzc3NXOffdgrY3t727fgzaVP5KWBnZ8e3r1KpaDkFNNfzegowAi4Cw7wu9gIIDKq4AAAAAAAAAAAAAAAAAAAAAAAAAC4z38SLi07I/d95HP8T99NHo/km40E5+x118OFs0s6DssJJOlH+h27QoivuwQchT4+L8vS4KJsNk/XOV5Wvb3bw/Px86G8+OzsbWyVGifvQfiHswWIscU+Pi6HjXrl6Elu+UeI6hyex5du1AIQQYnFxseubqtVq7AcSJu7rjYavMZzDk7bTWFQ3B651fc33o1+x5xsmblC+cZ8CAv9lTKlU6viG/r5+ZdVYKpXEQm1fPK8NtX1tobYfe+eHz3crFfmqOP+n6u8DNJNeqO1r6/zLkq+qi7/UFIB3uPM2SrfOvz98Oa8Bo+Sr8so/NQXw5KnZ7Fxfo2Sx86Pkq/S2L22ngOe1obZG8TbGnjMnstD5YfNVefWfygIIapRmYyyvTGaq86Pkm6sCaG0U5/DEd5Wcpc4Pk69qBd23ev/ijXv2CVgXyyuTor9PX9w85PvPAlDxkCeMoLh7zpy4e09t3LOHPFva8w2OWxT24JYAAAAAAAAAAAAAAAAAAAAAgPB+A2nxMpsTcydAAAAAAElFTkSuQmCC";
