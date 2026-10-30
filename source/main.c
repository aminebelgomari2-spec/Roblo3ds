/*---------------------------------------------------------------------------
  Roblo3DS - un mini "Roblox" pour Nintendo 3DS (3DS normale, 268 MHz)

  Controles :
    Circle pad       : bouger le personnage
    Ecran tactile    : glisser pour tourner la camera (style mobile)
    B                : sauter
    Croix < / >      : changer de slot d'inventaire (Epee / Bombe / Lance-roquette)
    L ou R           : utiliser l'objet du slot
    START            : reconstruire la carte (respawn)

  Moteur 3D : projection perspective calculee sur le CPU, chaque face est
  envoyee a citro2d comme quad colore avec une "profondeur" deduite de sa
  distance camera. Le depth buffer de citro2d (mode GEQUAL) trie tout,
  aucun tri logiciel necessaire.
---------------------------------------------------------------------------*/

#include <3ds.h>
#include <3ds/font.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*============================ Constantes ===========================*/

#define WORLD_X 28          /* largeur de la carte en blocs */
#define WORLD_Y 10          /* hauteur                      */
#define WORLD_Z 28          /* profondeur                   */

#define MAT_AIR    0
#define MAT_GRASS  1
#define MAT_DIRT   2
#define MAT_STONE  3
#define MAT_BRICK  4
#define MAT_WOOD   5

#define GRAVITY        20.0f
#define JUMP_SPEED     7.4f
#define WALK_SPEED     4.4f
#define PLAYER_RADIUS  0.30f
#define PLAYER_HEIGHT  1.70f
#define CAM_DIST       3.6f
#define FOCAL          320.0f
#define DEPTH_UNIT     0.0225f   /* 1.0 / (portee ~80 unites) */

#define MAX_PROJECTILES 16
#define MAX_BLASTS      8

static const float PI2 = 6.28318f;

/*--- petits types ---*/
typedef struct { float x, y, z; } Vec3;

typedef struct {
    bool  active;
    int   type;        /* 1 = bombe, 2 = roquette */
    float x, y, z;
    float vx, vy, vz;
    float fuse;
    float dist;
} Projectile;

typedef struct {
    bool  active;
    float x, y, z;
    float t;           /* avancement 0..1 */
    float radius;
} Blast;

/*--- monde voxel ---*/
static u8 vox[WORLD_Y][WORLD_Z][WORLD_X];

/*--- joueur ---*/
static Vec3  plPos;      /* y = pieds */
static float plVy;
static bool  plGround;
static float plYaw;
static float walkPhase;

/*--- camera ---*/
static float camYaw, camPitch;

/*--- camera : base recompute par frame ---*/
static float g_camX, g_camY, g_camZ;
static float g_fwdX, g_fwdY, g_fwdZ;
static float g_rightX, g_rightY, g_rightZ;
static float g_upX, g_upY, g_upZ;

/*--- inventaire : 0 = epee, 1 = bombe, 2 = lance-roquette ---*/
enum { IT_SWORD = 0, IT_BOMB = 1, IT_ROCKET = 2, IT_COUNT = 3 };
static const char* itemNames[IT_COUNT] = { "Epee", "Bombe", "Lance-roquette" };
static int   curItem = IT_SWORD;
static float useCooldown = 0.0f;

/*--- projectiles / explosions ---*/
static Projectile projs[MAX_PROJECTILES];
static Blast      blasts[MAX_BLASTS];

/*--- citro2d ---*/
static C3D_RenderTarget *g_top;
static C3D_RenderTarget *g_bottom;
static C2D_TextBuf       g_tbuf;

/*============================ Menu & chapeaux ===========================*/

/* etats : menu (choix chapeau) ou en jeu */
static bool inMenu  = true;
static float menuSpin = 0.0f;

typedef struct {
    const char* name;
    u32  col;        /* couleur principale du chapeau */
    bool hasVisor;   /* casquette : visiere devant */
    bool isCrown;    /* couronne : bandeau + pointes */
    bool isTall;     /* haut de forme : haut cylindrique */
} Hat;

/* NB : valeurs RGBA32 precalculees a la main car C2D_Color32() n'est pas
   une expression constante pour gcc (cast volatile interne) -> impossible
   dans un initialiseur statique. format : 0xAABBGGRR (r,g,b,a). */
static const Hat hats[4] = {
    { "Aucun",         0x00000000,   false, false, false }, /* (0,0,0,0)        */
    { "Casquette",     0xFF3A3AD6,   true,  false, false }, /* (214, 58, 58)    */
    { "Couronne",      0xFF3CCCFA,   false, true,  false }, /* (250, 204, 60)   */
    { "Haut de forme", 0xFF241E1E,   false, false, true  }, /* (30, 30, 36)     */
};
static const int HAT_COUNT = 4;
static int hatIdx = 0;

/*============================ Couleurs ===========================*/

#define CLR_CIEL     C2D_Color32(92, 169, 232, 255)
#define CLR_FOND_BAS C2D_Color32(32, 36, 48, 255)
#define CLR_TEXTE    C2D_Color32(255, 255, 255, 255)
#define CLR_TEXTE_OMB C2D_Color32(0, 0, 0, 170)

static u32 matTopColor(u8 m)
{
    switch (m) {
    case MAT_GRASS: return C2D_Color32(88, 184, 92, 255);
    case MAT_DIRT:  return C2D_Color32(146, 108, 74, 255);
    case MAT_STONE: return C2D_Color32(138, 138, 146, 255);
    case MAT_BRICK: return C2D_Color32(188, 72, 62, 255);
    case MAT_WOOD:  return C2D_Color32(178, 128, 78, 255);
    default:        return C2D_Color32(88, 184, 92, 255);
    }
}

static u32 matSideColor(u8 m)
{
    switch (m) {
    case MAT_GRASS: return C2D_Color32(146, 108, 74, 255);
    case MAT_DIRT:  return C2D_Color32(126, 92, 62, 255);
    case MAT_STONE: return C2D_Color32(112, 112, 120, 255);
    case MAT_BRICK: return C2D_Color32(158, 58, 50, 255);
    case MAT_WOOD:  return C2D_Color32(148, 104, 62, 255);
    default:        return C2D_Color32(146, 108, 74, 255);
    }
}

static u32 shadeColor(u32 c, float f)
{
    int r = (int)(((c >> 0) & 0xFF) * f);
    int g = (int)(((c >> 8) & 0xFF) * f);
    int b = (int)(((c >> 16) & 0xFF) * f);
    int a = (int)((c >> 24) & 0xFF);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return C2D_Color32((u8)r, (u8)g, (u8)b, (u8)a);
}

/*============================ Voxel world ===========================*/

static bool insideWorld(int x, int y, int z)
{
    return x >= 0 && x < WORLD_X && y >= 0 && y < WORLD_Y && z >= 0 && z < WORLD_Z;
}

static bool solidAt(int x, int y, int z)
{
    if (!insideWorld(x, y, z)) return false;
    return vox[y][z][x] != MAT_AIR;
}

static void buildWorld(void)
{
    int x, y, z;
    memset(vox, 0, sizeof(vox));

    /* baseplate en herbe */
    for (z = 0; z < WORLD_Z; z++)
        for (x = 0; x < WORLD_X; x++)
            vox[0][z][x] = MAT_GRASS;

    /* colline de terre au nord-ouest */
    for (z = 3; z <= 15; z++) {
        for (x = 3; x <= 15; x++) {
            float dx = (float)(x - 9), dz = (float)(z - 9);
            float d = sqrtf(dx * dx + dz * dz);
            int h = (int)(3.5f - d);
            if (h > 0) {
                if (h > 3) h = 3;
                for (y = 1; y <= h; y++)
                    vox[y][z][x] = MAT_DIRT;
                vox[h][z][x] = MAT_STONE; /* sommet de pierre */
            }
        }
    }

    /* tour en briques au sud-est (7x7, hauteur 7, crete en pierre) */
    for (z = 19; z <= 25; z++) {
        for (x = 19; x <= 25; x++) {
            bool bord = (x == 19 || x == 25 || z == 19 || z == 25);
            if (!bord) continue;
            for (y = 1; y <= 6; y++)
                vox[y][z][x] = MAT_BRICK;
        }
    }
    /* porte + fenetres */
    vox[1][19][22] = MAT_AIR;
    vox[2][19][22] = MAT_AIR;
    vox[3][19][21] = MAT_AIR;
    vox[3][19][23] = MAT_AIR;

    /* crete en pierre sur le sommet de la tour */
    for (z = 19; z <= 25; z++)
        for (x = 19; x <= 25; x++) {
            bool bord = (x == 19 || x == 25 || z == 19 || z == 25);
            if (bord) vox[6][z][x] = MAT_STONE;
        }

    /* escalier de pierre vers la crete */
    vox[1][18][19] = MAT_STONE;
    vox[2][17][19] = MAT_STONE;
    vox[3][16][19] = MAT_STONE;
    vox[4][15][19] = MAT_STONE;
    vox[5][15][20] = MAT_STONE;
    vox[6][15][21] = MAT_STONE;
    vox[6][15][22] = MAT_STONE;

    /* maison en bois au sud-ouest (6x5, murs h3 + toit plat) */
    for (z = 20; z <= 24; z++) {
        for (x = 4; x <= 9; x++) {
            bool bord = (x == 4 || x == 9 || z == 20 || z == 24);
            if (bord) {
                for (y = 1; y <= 3; y++)
                    vox[y][z][x] = MAT_WOOD;
            }
            vox[3][z][x] = MAT_WOOD; /* plafond partout */
        }
    }
    /* porte de la maison */
    vox[1][24][6] = MAT_AIR;
    vox[2][24][6] = MAT_AIR;

    /* quelques blocs de pierre eparses */
    vox[1][2][14]  = MAT_STONE;
    vox[1][3][14]  = MAT_STONE;
    vox[2][4][14]  = MAT_STONE;
    vox[1][26][9]  = MAT_STONE;
    vox[2][25][10] = MAT_STONE;
    vox[1][11][27] = MAT_STONE;
    vox[2][12][27] = MAT_STONE;
}

static void resetProjectiles(void)
{
    int i;
    for (i = 0; i < MAX_PROJECTILES; i++) projs[i].active = false;
    for (i = 0; i < MAX_BLASTS; i++) blasts[i].active = false;
}

/*============================ Explosions ===========================*/

static void spawnBlast(float x, float y, float z, float radius)
{
    int i;
    for (i = 0; i < MAX_BLASTS; i++) {
        if (!blasts[i].active) {
            blasts[i].active = true;
            blasts[i].x = x; blasts[i].y = y; blasts[i].z = z;
            blasts[i].t = 0.0f;
            blasts[i].radius = radius;
            return;
        }
    }
}

static void explodeAt(float x, float y, float z, float radius)
{
    int cx = (int)floorf(x);
    int cy = (int)floorf(y);
    int cz = (int)floorf(z);
    int r = (int)ceilf(radius) + 1;
    int ix, iy, iz;

    for (iy = cy - r; iy <= cy + r; iy++) {
        for (iz = cz - r; iz <= cz + r; iz++) {
            for (ix = cx - r; ix <= cx + r; ix++) {
                if (!insideWorld(ix, iy, iz)) continue;
                float bx = (float)ix + 0.5f, by = (float)iy + 0.5f, bz = (float)iz + 0.5f;
                float ddx = bx - x, ddy = by - y, ddz = bz - z;
                if (ddx * ddx + ddy * ddy + ddz * ddz <= radius * radius)
                    vox[iy][iz][ix] = MAT_AIR;
            }
        }
    }
}

/*=========================== Collision joueur ============================*/

static bool playerCollides(float px, float py, float pz)
{
    int x0 = (int)floorf(px - PLAYER_RADIUS), x1 = (int)floorf(px + PLAYER_RADIUS);
    int y0 = (int)floorf(py),                 y1 = (int)floorf(py + PLAYER_HEIGHT - 0.02f);
    int z0 = (int)floorf(pz - PLAYER_RADIUS), z1 = (int)floorf(pz + PLAYER_RADIUS);
    int x, y, z;
    for (y = y0; y <= y1; y++)
        for (z = z0; z <= z1; z++)
            for (x = x0; x <= x1; x++)
                if (solidAt(x, y, z)) return true;
    return false;
}

/*============================ Base camera ===========================*/

static void computeCameraBasis(void)
{
    float cy = cosf(camYaw), sy = sinf(camYaw);
    float cp = cosf(camPitch), sp = sinf(camPitch);
    g_fwdX = sy * cp; g_fwdY = sp; g_fwdZ = cy * cp;
    g_rightX = cy;    g_rightY = 0.0f; g_rightZ = -sy;
    g_upX = -sp * sy; g_upY = cp; g_upZ = -sp * cy;
}

/*============================ Rendu 3D ===========================*/

/* plan proche pour le clipping des faces (bloc/perso passes si proche
   de la camera qu'un coin se retrouve derriere elle) */
#define NEAR_CLIP_Z 0.15f

/* dessine un quad 3D (4 sommets, 2 triangles) AVEC clipping : si la face
   coupe le plan proche (un coin derriere la caméra), on la coupe au bon
   endroit au lieu de la faire disparaitre entierement (trous triangulaires). */
static bool drawQuad3D(const Vec3 v[4], u32 col, float depth)
{
    float vxv[4], vyv[4], vzv[4];
    float px[8], py[8];
    bool keep[4];
    bool any = false, all = true;
    int n = 0;
    int i;

    for (i = 0; i < 4; i++) {
        float wx = v[i].x - g_camX, wy = v[i].y - g_camY, wz = v[i].z - g_camZ;
        vxv[i] = wx * g_rightX + wy * g_rightY + wz * g_rightZ;
        vyv[i] = wx * g_upX + wy * g_upY + wz * g_upZ;
        vzv[i] = wx * g_fwdX + wy * g_fwdY + wz * g_fwdZ;
        keep[i] = (vzv[i] >= NEAR_CLIP_Z);
        if (keep[i]) any = true; else all = false;
    }
    /* tous les coins derriere la camera -> rien a dessiner */
    if (!any) return false;

    /* tout devant -> projection directe, non clippee */
    if (all) {
        for (i = 0; i < 4; i++) {
            px[i] = 200.0f + vxv[i] / vzv[i] * FOCAL;
            py[i] = 120.0f - vyv[i] / vzv[i] * FOCAL;
        }
        C2D_DrawTriangle(px[0], py[0], col, px[1], py[1], col, px[2], py[2], col, depth);
        C2D_DrawTriangle(px[1], py[1], col, px[2], py[2], col, px[3], py[3], col, depth);
        return true;
    }

    /* clip : parcourt les 4 cotes du quad, construit le polygone visible */
    for (i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        bool ka = keep[i], kb = keep[j];
        if (ka) {
            float z = vzv[i];
            px[n] = 200.0f + vxv[i] / z * FOCAL;
            py[n] = 120.0f - vyv[i] / z * FOCAL;
            n++;
        }
        if (ka != kb) {
            float t = (NEAR_CLIP_Z - vzv[i]) / (vzv[j] - vzv[i]);
            float ix = vxv[i] + t * (vxv[j] - vxv[i]);
            float iy = vyv[i] + t * (vyv[j] - vyv[i]);
            px[n] = 200.0f + ix / NEAR_CLIP_Z * FOCAL;
            py[n] = 120.0f - iy / NEAR_CLIP_Z * FOCAL;
            n++;
        }
        if (n >= 8) break;
    }
    if (n < 3) return false;

    /* eventail de triangles a partir du point 0 */
    for (i = 1; i + 1 < n; i++)
        C2D_DrawTriangle(px[0], py[0], col, px[i], py[i], col, px[i + 1], py[i + 1], col, depth);
    return true;
}

/* distance d'un point a la camera -> valeur de profondeur citro2d */
static float depthFor(float wx, float wy, float wz)
{
    float vx = wx - g_camX, vy = wy - g_camY, vz = wz - g_camZ;
    float d = sqrtf(vx * vx + vy * vy + vz * vz);
    float depth = 0.95f - d * DEPTH_UNIT;
    if (depth < -0.90f) depth = -0.90f;
    return depth;
}

/* taille du bloc : 1.0 unite. dir : 0 +Y, 1 -Y, 2 +X, 3 -X, 4 +Z, 5 -Z */
static void voxelFace(int dir, int x, int y, int z, Vec3 v[4])
{
    float X = (float)x, Y = (float)y, Z = (float)z;
    switch (dir) {
    case 0:
        v[0].x = X;     v[0].y = Y + 1; v[0].z = Z;
        v[1].x = X + 1; v[1].y = Y + 1; v[1].z = Z;
        v[2].x = X;     v[2].y = Y + 1; v[2].z = Z + 1;
        v[3].x = X + 1; v[3].y = Y + 1; v[3].z = Z + 1;
        break;
    case 1:
        v[0].x = X;     v[0].y = Y;     v[0].z = Z;
        v[1].x = X + 1; v[1].y = Y;     v[1].z = Z;
        v[2].x = X;     v[2].y = Y;     v[2].z = Z + 1;
        v[3].x = X + 1; v[3].y = Y;     v[3].z = Z + 1;
        break;
    case 2:
        v[0].x = X + 1; v[0].y = Y;     v[0].z = Z;
        v[1].x = X + 1; v[1].y = Y;     v[1].z = Z + 1;
        v[2].x = X + 1; v[2].y = Y + 1; v[2].z = Z;
        v[3].x = X + 1; v[3].y = Y + 1; v[3].z = Z + 1;
        break;
    case 3:
        v[0].x = X;     v[0].y = Y;     v[0].z = Z;
        v[1].x = X;     v[1].y = Y;     v[1].z = Z + 1;
        v[2].x = X;     v[2].y = Y + 1; v[2].z = Z;
        v[3].x = X;     v[3].y = Y + 1; v[3].z = Z + 1;
        break;
    case 4:
        v[0].x = X;     v[0].y = Y;     v[0].z = Z + 1;
        v[1].x = X + 1; v[1].y = Y;     v[1].z = Z + 1;
        v[2].x = X;     v[2].y = Y + 1; v[2].z = Z + 1;
        v[3].x = X + 1; v[3].y = Y + 1; v[3].z = Z + 1;
        break;
    default:
        v[0].x = X;     v[0].y = Y;     v[0].z = Z;
        v[1].x = X + 1; v[1].y = Y;     v[1].z = Z;
        v[2].x = X;     v[2].y = Y + 1; v[2].z = Z;
        v[3].x = X + 1; v[3].y = Y + 1; v[3].z = Z;
        break;
    }
}

static const int NEIGHBOR_DX[6] = { 0, 0, 1, -1, 0, 0 };
static const int NEIGHBOR_DY[6] = { 1, -1, 0, 0, 0, 0 };
static const int NEIGHBOR_DZ[6] = { 0, 0, 0, 0, 1, -1 };

static float faceShade(int dir)
{
    switch (dir) {
    case 0: return 1.00f;
    case 1: return 0.45f;
    case 2: return 0.82f;
    case 3: return 0.66f;
    case 4: return 0.90f;
    default: return 0.74f;
    }
}

static void drawVoxel(int x, int y, int z)
{
    u8 m = vox[y][z][x];
    if (m == MAT_AIR) return;

    int dir;
    for (dir = 0; dir < 6; dir++) {
        int nx = x + NEIGHBOR_DX[dir];
        int ny = y + NEIGHBOR_DY[dir];
        int nz = z + NEIGHBOR_DZ[dir];
        if (insideWorld(nx, ny, nz) && vox[ny][nz][nx] != MAT_AIR) continue;

        Vec3 v[4];
        voxelFace(dir, x, y, z, v);

        /* centre de la FACE (pas du cube) -> profondeur exacte par face.
           Avant : toutes les faces d'un cube avaient la meme profondeur,
           et une face arriere redessinee apres la face avant l'ecrasait
           (test GEQUAL accepte les valeurs egales) => cubes "transparents". */
        float fcx = (v[0].x + v[1].x + v[2].x + v[3].x) * 0.25f;
        float fcy = (v[0].y + v[1].y + v[2].y + v[3].y) * 0.25f;
        float fcz = (v[0].z + v[1].z + v[2].z + v[3].z) * 0.25f;

        /* culling CPU : elimine les faces pas tournees vers la camera.
           normale de la face = direction NEIGHBOR de cette face. */
        float tox = g_camX - fcx, toy = g_camY - fcy, toz = g_camZ - fcz;
        float dot = (float)NEIGHBOR_DX[dir] * tox
                  + (float)NEIGHBOR_DY[dir] * toy
                  + (float)NEIGHBOR_DZ[dir] * toz;
        if (dot <= 0.0f) continue;

        float depth = depthFor(fcx, fcy, fcz);
        u32 c = shadeColor((dir == 0) ? matTopColor(m) : matSideColor(m), faceShade(dir));
        drawQuad3D(v, c, depth);
    }
}

/* boite orientee par yaw (perso, objets) : centre + tailles + 3 couleurs.
   Profondeur par face + culling CPU, sinon les faces des boites
   s'ecrasent entre elles (bras "dans" le torse, etc). */
static void drawBoxYaw(float cx, float cy, float cz, float sx, float sy, float sz,
                       float yaw, float dist, u32 topC, u32 sideXA, u32 sideZA)
{
    float cyy = cosf(yaw), syy = sinf(yaw);
    float hx = sx * 0.5f, hz = sz * 0.5f;
    float y0 = cy - sy * 0.5f, y1 = cy + sy * 0.5f;
    float lx[4], lz[4];
    Vec3 c[8];
    int i;

    (void)dist;
    for (i = 0; i < 4; i++) {
        float px = (i & 1) ? hx : -hx;
        float pz = (i & 2) ? hz : -hz;
        lx[i] = px * cyy + pz * syy;
        lz[i] = -px * syy + pz * cyy;
    }

    /* 8 sommets monde : 0..3 dessous, 4..7 dessus */
    for (i = 0; i < 4; i++) {
        c[i].x     = cx + lx[i]; c[i].y     = y0; c[i].z     = cz + lz[i];
        c[i + 4].x = cx + lx[i]; c[i + 4].y = y1; c[i + 4].z = cz + lz[i];
    }

    {
        u32 cT  = topC;
        u32 cXa = shadeColor(sideXA, 0.85f);
        u32 cXb = shadeColor(sideXA, 0.70f);
        u32 cZa = shadeColor(sideZA, 0.95f);
        u32 cZb = shadeColor(sideZA, 0.68f);
        u32 cUn = shadeColor(sideXA, 0.45f);

        /* {i0,i1,i2,i3,couleur} : normales orientees vers l'exterieur */
        const int FI[6][4] = {
            { 4, 6, 7, 5 },  /* dessus  (+Y) */
            { 0, 1, 3, 2 },  /* dessous (-Y) */
            { 1, 0, 4, 5 },  /* cote -Z */
            { 2, 3, 7, 6 },  /* cote +Z */
            { 3, 1, 5, 7 },  /* cote +X */
            { 0, 2, 6, 4 },  /* cote -X */
        };
        const u32 FC[6] = { cT, cUn, cZb, cZa, cXa, cXb };

        for (i = 0; i < 6; i++) {
            const Vec3 *a = &c[FI[i][0]];
            const Vec3 *b = &c[FI[i][1]];
            const Vec3 *d = &c[FI[i][2]];
            const Vec3 *e = &c[FI[i][3]];

            float e1x = b->x - a->x, e1y = b->y - a->y, e1z = b->z - a->z;
            float e2x = d->x - a->x, e2y = d->y - a->y, e2z = d->z - a->z;

            float nX = e1y * e2z - e1z * e2y;
            float nY = e1z * e2x - e1x * e2z;
            float nZ = e1x * e2y - e1y * e2x;

            float fcx = (a->x + b->x + d->x + e->x) * 0.25f;
            float fcy = (a->y + b->y + d->y + e->y) * 0.25f;
            float fcz = (a->z + b->z + d->z + e->z) * 0.25f;

            float tox = g_camX - fcx, toy = g_camY - fcy, toz = g_camZ - fcz;
            if (nX * tox + nY * toy + nZ * toz <= 0.0f) continue;

            {
                Vec3 v[4];
                v[0] = c[FI[i][0]];
                v[1] = c[FI[i][1]];
                v[2] = c[FI[i][2]];
                v[3] = c[FI[i][3]];
                drawQuad3D(v, FC[i], depthFor(fcx, fcy, fcz));
            }
        }
    }
}

/*============================ Personnage ===========================*/

/* chapeau du perso : dessine au-dessus de la tete, oriente comme lui */
static void drawHat(void)
{
    const Hat* h = &hats[hatIdx];
    float fx = sinf(plYaw), fz = cosf(plYaw); /* avant du corps */
    float topY = plPos.y + 1.80f;             /* sommet de la tete */

    if (hatIdx == 0) return;

    if (h->isCrown) {
        /* bandeau + 3 pointes or alignees avant/centre/arriere */
        drawBoxYaw(plPos.x, topY + 0.07f, plPos.z, 0.50f, 0.14f, 0.50f, plYaw, 1.0f,
                   h->col, h->col, h->col);
        drawBoxYaw(plPos.x - fx * 0.15f, topY + 0.20f, plPos.z - fz * 0.15f,
                   0.10f, 0.15f, 0.10f, plYaw, 1.0f, h->col, h->col, h->col);
        drawBoxYaw(plPos.x, topY + 0.20f, plPos.z,
                   0.10f, 0.15f, 0.10f, plYaw, 1.0f, h->col, h->col, h->col);
        drawBoxYaw(plPos.x + fx * 0.15f, topY + 0.20f, plPos.z + fz * 0.15f,
                   0.10f, 0.15f, 0.10f, plYaw, 1.0f, h->col, h->col, h->col);
    } else if (h->isTall) {
        /* haut de forme : rebord large + cylindre haut */
        drawBoxYaw(plPos.x, topY + 0.03f, plPos.z, 0.66f, 0.06f, 0.66f, plYaw, 1.0f,
                   h->col, h->col, h->col);
        drawBoxYaw(plPos.x, topY + 0.29f, plPos.z, 0.40f, 0.48f, 0.40f, plYaw, 1.0f,
                   h->col, h->col, h->col);
    } else {
        /* casquette : calotte + visiere vers l'avant du corps */
        drawBoxYaw(plPos.x, topY + 0.06f, plPos.z, 0.50f, 0.12f, 0.50f, plYaw, 1.0f,
                   h->col, h->col, h->col);
        drawBoxYaw(plPos.x + fx * 0.32f, topY + 0.02f, plPos.z + fz * 0.32f,
                   0.44f, 0.05f, 0.26f, plYaw, 1.0f,
                   h->col, h->col, h->col);
    }
}

static void drawPlayer(void)
{
    float swing = sinf(walkPhase) * 0.10f;
    u32 skin  = C2D_Color32(250, 205, 110, 255);
    u32 shirt = C2D_Color32(66, 120, 214, 255);
    u32 pants = C2D_Color32(74, 160, 72, 255);

    /* axe lateral du corps (perpendiculaire a son regard) */
    float brx = cosf(plYaw), brz = -sinf(plYaw);

    /* jambes : DEUX jambes separees, bien visibles (0 a 0.60) */
    drawBoxYaw(plPos.x - brx * 0.13f, plPos.y + 0.30f, plPos.z - brz * 0.13f,
               0.22f, 0.60f, 0.26f, plYaw, 1.0f, pants, pants, pants);
    drawBoxYaw(plPos.x + brx * 0.13f, plPos.y + 0.30f, plPos.z + brz * 0.13f,
               0.22f, 0.60f, 0.26f, plYaw, 1.0f, pants, pants, pants);
    /* torse (0.60 a 1.35) */
    drawBoxYaw(plPos.x, plPos.y + 0.98f, plPos.z, 0.56f, 0.75f, 0.32f, plYaw, 1.0f,
               shadeColor(shirt, 0.8f), shirt, shirt);
    /* bras : un de chaque cote (5-tes aux epaules), balancement oppose */
    drawBoxYaw(plPos.x - brx * 0.40f, plPos.y + 0.98f + swing * 0.5f, plPos.z - brz * 0.40f,
               0.20f, 0.78f, 0.26f, plYaw, 1.0f, skin, skin, skin);
    drawBoxYaw(plPos.x + brx * 0.40f, plPos.y + 0.98f - swing * 0.5f, plPos.z + brz * 0.40f,
               0.20f, 0.78f, 0.26f, plYaw, 1.0f, skin, skin, skin);
    /* tete (1.35 a 1.80) */
    drawBoxYaw(plPos.x, plPos.y + 1.58f, plPos.z, 0.45f, 0.45f, 0.45f, plYaw, 1.0f,
               skin, skin, skin);
    /* chapeau choisi dans le menu */
    drawHat();
}

/*============================ Texte ===========================*/

/* La police systeme doit etre disponible, sinon citro2d declenche un
   svcBreak (reboot de la console). fontEnsureMapped est appelee AVANT
   C2D_TextBufNew pour eviter ce panic : si elle echoue, on dessine le
   jeu sans texte plutot que de faire plantuer la machine. */
static bool fontOk = false;

static void drawTextShadow(const char* s, float x, float y, float scale, u32 col, int center)
{
    C2D_Text t;
    u32 flags = C2D_WithColor | (center ? C2D_AlignCenter : C2D_AlignLeft);
    if (!fontOk) return;
    C2D_TextBufClear(g_tbuf);
    C2D_TextParse(&t, g_tbuf, s);
    C2D_TextOptimize(&t);
    /* profondeur ~1.0 : avec le test GEQUAL le texte gagne TOUJOURS
       sur le decor (z plus grand = plus proche ici) */
    C2D_DrawText(&t, flags, x + 2.0f, y + 2.0f, 0.96f, scale, scale, CLR_TEXTE_OMB);
    C2D_DrawText(&t, flags, x, y, 0.97f, scale, scale, col);
}

/*============================ Inventaire (ecran bas) ===========================*/

static void drawSlotIcon(int item, float sx, float sy)
{
    switch (item) {
    case IT_SWORD: /* lame grise verticale + garde + poignee */
        C2D_DrawRectSolid(sx + 26.0f, sy + 8.0f, 0.4f, 4.0f, 22.0f, C2D_Color32(205, 205, 210, 255));
        C2D_DrawRectSolid(sx + 21.0f, sy + 30.0f, 0.4f, 14.0f, 3.0f, C2D_Color32(165, 115, 45, 255));
        C2D_DrawRectSolid(sx + 26.0f, sy + 33.0f, 0.4f, 4.0f, 9.0f, C2D_Color32(120, 80, 30, 255));
        break;
    case IT_BOMB: /* boule noire + meche */
        C2D_DrawCircleSolid(sx + 28.0f, sy + 28.0f, 0.4f, 12.0f, C2D_Color32(30, 30, 36, 255));
        C2D_DrawRectSolid(sx + 26.0f, sy + 13.0f, 0.4f, 3.0f, 6.0f, C2D_Color32(150, 150, 155, 255));
        C2D_DrawRectSolid(sx + 21.0f, sy + 9.0f, 0.4f, 3.0f, 6.0f, C2D_Color32(255, 170, 40, 255));
        break;
    case IT_ROCKET: /* tube gris + ogive rouge */
        C2D_DrawRectSolid(sx + 10.0f, sy + 22.0f, 0.4f, 24.0f, 10.0f, C2D_Color32(95, 105, 120, 255));
        C2D_DrawTriangle(sx + 34.0f, sy + 22.0f, C2D_Color32(200, 80, 60, 255),
                         sx + 42.0f, sy + 27.0f, C2D_Color32(200, 80, 60, 255),
                         sx + 34.0f, sy + 32.0f, C2D_Color32(200, 80, 60, 255), 0.4f);
        break;
    default:
        break;
    }
}

static void drawHotbar(void)
{
    int i;
    char lbl[8];
    C2D_Text t;

    for (i = 0; i < IT_COUNT; i++) {
        float sx = 68.0f + i * 66.0f, sy = 184.0f;
        u32 bg = (i == curItem) ? C2D_Color32(255, 255, 255, 70) : C2D_Color32(255, 255, 255, 30);
        u32 border = (i == curItem) ? C2D_Color32(255, 255, 255, 255)
                                    : C2D_Color32(255, 255, 255, 110);
        C2D_DrawRectSolid(sx, sy, 0.3f, 56.0f, 48.0f, bg);
        C2D_DrawRectSolid(sx, sy, 0.35f, 56.0f, 2.0f, border);
        C2D_DrawRectSolid(sx, sy + 46.0f, 0.35f, 56.0f, 2.0f, border);
        C2D_DrawRectSolid(sx, sy, 0.35f, 2.0f, 48.0f, border);
        C2D_DrawRectSolid(sx + 54.0f, sy, 0.35f, 2.0f, 48.0f, border);
        drawSlotIcon(i, sx, sy);
    }

    for (i = 0; i < IT_COUNT; i++) {
        snprintf(lbl, sizeof(lbl), "%d", i + 1);
        if (!fontOk) return;
        C2D_TextBufClear(g_tbuf);
        C2D_TextParse(&t, g_tbuf, lbl);
        C2D_TextOptimize(&t);
        C2D_DrawText(&t, C2D_AtBaseline | C2D_WithColor | C2D_AlignCenter,
                     68.0f + i * 66.0f + 28.0f, 234.0f, 0.5f, 0.42f, 0.42f,
                     C2D_Color32(255, 255, 255, 200));
    }
}

/*============================ Buffers de rendu par scene ===========================*/

/* rectangle touche ? (x0,y0)-(x1,y1) sur l'ecran du bas */
static bool tapRect(u32 kDown, const touchPosition* t, float x0, float y0, float x1, float y1)
{
    if (!(kDown & KEY_TOUCH)) return false;
    return (float)t->px >= x0 && (float)t->px <= x1 &&
           (float)t->py >= y0 && (float)t->py <= y1;
}

static void drawMenuTop(void)
{
    C2D_TargetClear(g_top, CLR_CIEL);
    C2D_SceneBegin(g_top);

    /* petit sol sous l'aperçu du perso */
    {
        Vec3 v[4];
        v[0].x = plPos.x - 6.0f; v[0].y = 1.0f; v[0].z = plPos.z - 6.0f;
        v[1].x = plPos.x + 6.0f; v[1].y = 1.0f; v[1].z = plPos.z - 6.0f;
        v[2].x = plPos.x - 6.0f; v[2].y = 1.0f; v[2].z = plPos.z + 6.0f;
        v[3].x = plPos.x + 6.0f; v[3].y = 1.0f; v[3].z = plPos.z + 6.0f;
        drawQuad3D(v, C2D_Color32(88, 184, 92, 255), depthFor(plPos.x, 1.0f, plPos.z));
    }

    /* le perso tourne lentement, avec le chapeau selectionne */
    drawPlayer();

    drawTextShadow("Roblo3DS", 160.0f, 8.0f, 0.62f, C2D_Color32(255, 240, 130, 255), 1);
    drawTextShadow(hats[hatIdx].name, 160.0f, 40.0f, 0.52f, C2D_Color32(255, 230, 110, 255), 1);
    drawTextShadow("A / START / bouton JOUER pour jouer", 160.0f, 214.0f, 0.40f, CLR_TEXTE, 1);
}

static void drawMenuBottom(void)
{
    u32 btnGreen = C2D_Color32(88, 184, 92, 255);
    u32 btnBlue  = C2D_Color32(66, 132, 196, 255);
    u32 white    = C2D_Color32(255, 255, 255, 255);

    C2D_TargetClear(g_bottom, CLR_FOND_BAS);
    C2D_SceneBegin(g_bottom);

    /* gros bouton JOUER */
    C2D_DrawRectSolid(60.0f, 26.0f, 0.2f, 200.0f, 58.0f, btnGreen);
    C2D_DrawRectSolid(60.0f, 26.0f, 0.35f, 200.0f, 4.0f, white);
    C2D_DrawRectSolid(60.0f, 80.0f, 0.35f, 200.0f, 4.0f, white);
    C2D_DrawRectSolid(60.0f, 26.0f, 0.35f, 4.0f, 58.0f, white);
    C2D_DrawRectSolid(256.0f, 26.0f, 0.35f, 4.0f, 58.0f, white);
    drawTextShadow("JOUER", 160.0f, 44.0f, 0.62f, CLR_TEXTE, 1);

    /* selection de chapeau : fleches + nom */
    C2D_DrawRectSolid(22.0f, 118.0f, 0.2f, 66.0f, 52.0f, btnBlue);
    C2D_DrawRectSolid(232.0f, 118.0f, 0.2f, 66.0f, 52.0f, btnBlue);
    drawTextShadow("<", 55.0f, 130.0f, 0.70f, CLR_TEXTE, 1);
    drawTextShadow(">", 265.0f, 130.0f, 0.70f, CLR_TEXTE, 1);
    drawTextShadow(hats[hatIdx].name, 160.0f, 130.0f, 0.50f, C2D_Color32(255, 230, 110, 255), 1);
    drawTextShadow("Touche : chapeau / arriere", 160.0f, 180.0f, 0.42f, C2D_Color32(255, 255, 255, 210), 1);
    drawTextShadow("L ou R = chapeau", 160.0f, 196.0f, 0.42f, CLR_TEXTE, 1);
}

static void drawTopScreen(void)
{
    int x, y, z;
    C2D_TargetClear(g_top, CLR_CIEL);
    C2D_SceneBegin(g_top);

    /* nuages en gros quads blancs semi-transparents */
    {
        int d;
        for (d = 0; d < 3; d++) {
            float cx = 6.0f + d * 10.0f;
            float cy = 8.6f + (d & 1) * 1.1f;
            float cz = 20.0f + d * 2.5f;
            Vec3 v[4];
            v[0].x = cx - 2.5f; v[0].y = cy; v[0].z = cz - 2.5f;
            v[1].x = cx + 2.5f; v[1].y = cy; v[1].z = cz - 2.5f;
            v[2].x = cx - 2.5f; v[2].y = cy; v[2].z = cz + 2.5f;
            v[3].x = cx + 2.5f; v[3].y = cy; v[3].z = cz + 2.5f;
            drawQuad3D(v, C2D_Color32(255, 255, 255, 170), -0.90f); /* tout au fond */
        }
    }

    /* monde voxel */
    for (y = WORLD_Y - 1; y >= 0; y--)
        for (z = 0; z < WORLD_Z; z++)
            for (x = 0; x < WORLD_X; x++)
                drawVoxel(x, y, z);

    /* joueur */
    drawPlayer();

    /* projectiles */
    {
        int i;
        u32 bombCol = C2D_Color32(22, 22, 28, 255);
        u32 rockCol = C2D_Color32(90, 100, 118, 255);
        for (i = 0; i < MAX_PROJECTILES; i++) {
            if (!projs[i].active) continue;
            if (projs[i].type == 1)
                drawBoxYaw(projs[i].x, projs[i].y, projs[i].z, 0.30f, 0.30f, 0.30f, 0.0f, 1.0f,
                           bombCol, bombCol, bombCol);
            else
                drawBoxYaw(projs[i].x, projs[i].y, projs[i].z, 0.44f, 0.20f, 0.20f, 0.0f, 1.0f,
                           rockCol, rockCol, rockCol);
        }
    }

    /* halos d'explosion */
    {
        int i;
        for (i = 0; i < MAX_BLASTS; i++) {
            if (!blasts[i].active) continue;
            float a = 1.0f - blasts[i].t;
            if (a <= 0.0f) continue;
            u32 c = C2D_Color32(255, (u8)(150 + (int)(80 * a)), 40, (u8)(a * 190));
            float r = blasts[i].radius * (0.35f + blasts[i].t * 0.65f);
            float cx = blasts[i].x, cy = blasts[i].y, cz = blasts[i].z;
            Vec3 v[4];

            /* face horizontale */
            v[0].x = cx - r; v[0].y = cy; v[0].z = cz - r;
            v[1].x = cx + r; v[1].y = cy; v[1].z = cz - r;
            v[2].x = cx - r; v[2].y = cy; v[2].z = cz + r;
            v[3].x = cx + r; v[3].y = cy; v[3].z = cz + r;
            drawQuad3D(v, c, depthFor(cx, cy, cz));

            /* face verticale */
            v[0].x = cx - r; v[0].y = cy - r; v[0].z = cz;
            v[1].x = cx + r; v[1].y = cy - r; v[1].z = cz;
            v[2].x = cx - r; v[2].y = cy + r; v[2].z = cz;
            v[3].x = cx + r; v[3].y = cy + r; v[3].z = cz;
            drawQuad3D(v, c, depthFor(cx, cy, cz));
        }
    }

    /* HUD : titre + objet + aide */
    drawTextShadow("Roblo3DS", 8.0f, 4.0f, 0.48f, C2D_Color32(255, 240, 130, 255), 0);
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "Objet : %s", itemNames[curItem]);
        drawTextShadow(buf, 8.0f, 26.0f, 0.44f, CLR_TEXTE, 0);
    }
    drawTextShadow("L/R : utiliser   B : sauter", 8.0f, 46.0f, 0.40f,
                   C2D_Color32(255, 255, 255, 210), 0);
    drawTextShadow("Croix G/D : changer d'objet", 8.0f, 62.0f, 0.40f,
                   C2D_Color32(255, 255, 255, 210), 0);
    drawTextShadow("START : reconstruire la carte", 8.0f, 78.0f, 0.40f,
                   C2D_Color32(255, 255, 255, 210), 0);
}

static void drawBottomScreen(void)
{
    C2D_TargetClear(g_bottom, CLR_FOND_BAS);
    C2D_SceneBegin(g_bottom);

    /* indique la zone draggable */
    C2D_DrawRectSolid(0, 0, 0.10f, 320, 68, C2D_Color32(66, 132, 196, 255));
    C2D_DrawRectSolid(0, 68, 0.10f, 320, 116, C2D_Color32(48, 104, 152, 255));

    drawTextShadow("Glisser = camera", 160.0f, 10.0f, 0.42f, CLR_TEXTE, 1);
    drawTextShadow("L/R = utiliser l'objet", 160.0f, 30.0f, 0.42f, CLR_TEXTE, 1);
    drawTextShadow(itemNames[curItem], 160.0f, 54.0f, 0.55f, C2D_Color32(255, 230, 110, 255), 1);

    drawHotbar();
}

/*============================ Armes ===========================*/

static void swingSword(void)
{
    /* l'epee suit le regard du PERSONNAGE (plYaw), pas celui de la camera */
    float dx = sinf(plYaw), dz = cosf(plYaw);
    float f;
    for (f = 0.2f; f <= 2.8f; f += 0.2f) {
        int tx = (int)floorf(plPos.x + dx * f);
        int ty = (int)floorf(plPos.y + 1.2f);
        int tz = (int)floorf(plPos.z + dz * f);
        if (insideWorld(tx, ty, tz) && vox[ty][tz][tx] != MAT_AIR) {
            vox[ty][tz][tx] = MAT_AIR;
            return;
        }
    }
}

static void throwBomb(void)
{
    int i;
    for (i = 0; i < MAX_PROJECTILES; i++) {
        if (projs[i].active) continue;
        projs[i].active = true;
        projs[i].type = 1;
        projs[i].x = plPos.x; projs[i].y = plPos.y + 1.2f; projs[i].z = plPos.z;
        projs[i].vx = g_fwdX * 6.0f;
        projs[i].vy = g_fwdY * 6.0f + 3.6f;
        projs[i].vz = g_fwdZ * 6.0f;
        projs[i].fuse = 2.6f;
        projs[i].dist = 0.0f;
        return;
    }
}

static void fireRocket(void)
{
    int i;
    for (i = 0; i < MAX_PROJECTILES; i++) {
        if (projs[i].active) continue;
        projs[i].active = true;
        projs[i].type = 2;
        projs[i].x = plPos.x; projs[i].y = plPos.y + 1.3f; projs[i].z = plPos.z;
        projs[i].vx = g_fwdX * 26.0f;
        projs[i].vy = g_fwdY * 26.0f;
        projs[i].vz = g_fwdZ * 26.0f;
        projs[i].fuse = 3.2f;
        projs[i].dist = 0.0f;
        return;
    }
}

static void respawn(void)
{
    plPos.x = 14.5f; plPos.z = 14.5f; plPos.y = 2.2f;
    plVy = 0.0f;
    plGround = false;
}

/*============================ Boucle principale ===========================*/

int main(void)
{
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(24576); /* marge pour toutes les faces visibles d'une frame */
    C2D_Prepare();

    g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    /* charger la police systeme soi-meme pour etre sur de ne jamais
       passer par le svcBreak interne de citro2d */
    fontOk = R_SUCCEEDED(fontEnsureMapped());
    g_tbuf = fontOk ? C2D_TextBufNew(4096) : NULL;

    buildWorld();
    resetProjectiles();
    respawn();
    plPos.y = 1.0f; /* demarre dans le menu : pose sur le sol de l'apercu */
    camYaw = -2.30f;
    camPitch = -0.20f;

    u64 lastMs = osGetTime();
    bool dragging = false;
    touchPosition prevTouch;
    prevTouch.px = 0;
    prevTouch.py = 0;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        touchPosition touch;
        hidTouchRead(&touch);
        circlePosition cp;
        hidCircleRead(&cp);

        u64 nowMs = osGetTime();
        float dt = (float)(nowMs - lastMs) / 1000.0f;
        lastMs = nowMs;
        if (dt > 0.05f) dt = 0.05f;
        if (dt < 0.0005f) dt = 0.0005f;

        /*--- cooldowns ---*/
        if (useCooldown > 0.0f) useCooldown -= dt;

        /*============================ MENU ===========================*/
        if (inMenu) {
            menuSpin += dt * 0.6f;
            if ((kDown & (KEY_L | KEY_DLEFT)) ||
                tapRect(kDown, &touch, 22.0f, 118.0f, 88.0f, 170.0f)) {
                hatIdx--;
                if (hatIdx < 0) hatIdx = HAT_COUNT - 1;
            }
            if ((kDown & (KEY_R | KEY_DRIGHT)) ||
                tapRect(kDown, &touch, 232.0f, 118.0f, 298.0f, 170.0f)) {
                hatIdx++;
                if (hatIdx >= HAT_COUNT) hatIdx = 0;
            }
            if ((kDown & (KEY_A | KEY_START)) ||
                tapRect(kDown, &touch, 60.0f, 26.0f, 260.0f, 84.0f)) {
                inMenu = false;
                respawn(); /* place le joueur sur la vraie carte */
                camYaw = -2.30f;
                camPitch = -0.20f;
                walkPhase = 0.0f;
            }
        } else {
        /*============================ JEU ===========================*/
        /*--- retour au menu (SELECT) ---*/
        if (kDown & KEY_SELECT) {
            inMenu = true;
            respawn();
            plPos.y = 1.0f; /* posé sur le sol de l'apercu */
        }

        /*--- L/R : utiliser l'objet ---*/
        if ((kDown & KEY_L) || (kDown & KEY_R)) {
            if (useCooldown <= 0.0f) {
                if (curItem == IT_SWORD) {
                    swingSword();
                    useCooldown = 0.35f;
                } else if (curItem == IT_BOMB) {
                    throwBomb();
                    useCooldown = 1.2f;
                } else {
                    fireRocket();
                    useCooldown = 1.6f;
                }
            }
        }

        /*--- croix G/D : slot d'inventaire ---*/
        if (kDown & KEY_DLEFT) {
            curItem--;
            if (curItem < 0) curItem = IT_COUNT - 1;
        }
        if (kDown & KEY_DRIGHT) {
            curItem++;
            if (curItem >= IT_COUNT) curItem = 0;
        }

        /*--- START : reconstruire la carte ---*/
        if (kDown & KEY_START) {
            buildWorld();
            resetProjectiles();
            respawn();
        }

        /*--- camera tactile ---*/
        if (kHeld & KEY_TOUCH) {
            if (dragging) {
                float dx = (float)touch.px - (float)prevTouch.px;
                float dy = (float)touch.py - (float)prevTouch.py;
                camYaw += dx * 0.0085f;
                camPitch -= dy * 0.0062f; /* glisser vers le bas = regarder vers le bas */
                if (camPitch > 1.25f) camPitch = 1.25f;
                if (camPitch < -1.25f) camPitch = -1.25f;
            }
            dragging = true;
            prevTouch = touch;
        } else {
            dragging = false;
        }

        /*--- deplacement circle pad (relatif a la camera) ---*/
        computeCameraBasis();

        float hw = (float)cp.dx, hv = (float)cp.dy;
        if (hw < 30.0f && hw > -30.0f) hw = 0.0f;
        if (hv < 30.0f && hv > -30.0f) hv = 0.0f;
        float mx = g_rightX * hw + g_fwdX * hv;   /* +hv = pousser le pad vers l'avant */
        float mz = g_rightZ * hw + g_fwdZ * hv;
        float mlen = sqrtf(mx * mx + mz * mz);
        if (mlen > 0.001f) {
            mx = (mx / mlen) * WALK_SPEED;
            mz = (mz / mlen) * WALK_SPEED;
        }

        /*--- saut + gravite ---*/
        if ((kDown & KEY_B) && plGround) {
            plVy = JUMP_SPEED;
            plGround = false;
        }
        plVy -= GRAVITY * dt;
        if (plVy < -22.0f) plVy = -22.0f;

        { /* deplacement axe par axe */
            float oldX = plPos.x, oldY = plPos.y, oldZ = plPos.z;

            plPos.x += mx * dt;
            if (playerCollides(plPos.x, plPos.y, plPos.z)) plPos.x = oldX;

            plPos.z += mz * dt;
            if (playerCollides(plPos.x, plPos.y, plPos.z)) plPos.z = oldZ;

            plPos.y += plVy * dt;
            if (playerCollides(plPos.x, plPos.y, plPos.z)) {
                if (plVy < 0.0f) plGround = true;
                plPos.y = oldY;
                plVy = 0.0f;
            } else {
                plGround = false;
            }

            if (plPos.y < -8.0f) respawn();
        }

        /*--- orientation du corps + animation marche ---*/
        {
            float diff;
            if (mlen > 0.001f) {
                float wantYaw = atan2f(mx, mz);
                diff = wantYaw - plYaw;
                while (diff > 3.1415927f) diff -= PI2;
                while (diff < -3.1415927f) diff += PI2;
                plYaw += diff * 0.30f;
                walkPhase += dt * 11.0f;
            } else {
                walkPhase = 0.0f;
            }
        }

        /*--- projectiles ---*/
        {
            int i;
            for (i = 0; i < MAX_PROJECTILES; i++) {
                if (!projs[i].active) continue;
                Projectile* p = &projs[i];

                p->fuse -= dt;
                if (p->fuse <= 0.0f || (p->type == 2 && p->dist > 70.0f)) {
                    spawnBlast(p->x, p->y, p->z, p->type == 1 ? 3.0f : 2.4f);
                    explodeAt(p->x, p->y, p->z, p->type == 1 ? 3.0f : 2.4f);
                    p->active = false;
                    continue;
                }

                if (p->type == 1) {
                    p->vy -= GRAVITY * dt;
                    if (p->y + p->vy * dt <= 0.05f) {
                        /* la bombe rebondit une fois puis eclate au sol */
                        p->y = 0.05f;
                        p->vy = -p->vy * 0.35f;
                        if (fabsf(p->vy) < 1.0f) {
                            spawnBlast(p->x, p->y + 0.3f, p->z, 3.0f);
                            explodeAt(p->x, p->y + 0.3f, p->z, 3.0f);
                            p->active = false;
                            continue;
                        }
                    }
                } else {
                    p->dist += 26.0f * dt;
                }

                float nx = p->x + p->vx * dt;
                float ny = p->y + p->vy * dt;
                float nz = p->z + p->vz * dt;

                bool hit = solidAt((int)floorf(nx), (int)floorf(p->y), (int)floorf(p->z))
                        || solidAt((int)floorf(p->x), (int)floorf(ny), (int)floorf(p->z))
                        || solidAt((int)floorf(p->x), (int)floorf(p->y), (int)floorf(nz));

                if (hit) {
                    spawnBlast(p->x, p->y, p->z, p->type == 1 ? 3.0f : 2.4f);
                    explodeAt(p->x, p->y, p->z, p->type == 1 ? 3.0f : 2.4f);
                    p->active = false;
                    continue;
                }

                p->x = nx; p->y = ny; p->z = nz;
            }
        }

        /*--- animations d'explosion ---*/
        {
            int i;
            for (i = 0; i < MAX_BLASTS; i++) {
                if (!blasts[i].active) continue;
                blasts[i].t += dt / 0.45f;
                if (blasts[i].t >= 1.0f) blasts[i].active = false;
            }
        }

        /*--- position camera (3e personne orbite) ---*/
        {
            g_camX = plPos.x - g_fwdX * CAM_DIST;
            g_camY = plPos.y + 1.25f - g_fwdY * CAM_DIST;
            g_camZ = plPos.z - g_fwdZ * CAM_DIST;
            if (g_camY < 0.4f) g_camY = 0.4f;
        }
        } /* fin du bloc "en jeu" */

        /*--- rendu : menu ou jeu ---*/
        if (inMenu) {
            /* aperdu : le perso tourne au centre, chapeau visible */
            menuSpin += dt * 0.6f;
            camYaw = menuSpin;
            camPitch = -0.30f;
            computeCameraBasis();
            g_camX = plPos.x - g_fwdX * 6.0f;
            g_camY = plPos.y + 1.60f - g_fwdY * 6.0f;
            g_camZ = plPos.z - g_fwdZ * 6.0f;
            if (g_camY < 0.4f) g_camY = 0.4f;
        } else {
            menuSpin = camYaw; /* reprise propre du jeu */
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_Prepare();
        if (inMenu) {
            plPos.y = 1.0f; /* sol de l'apercu a y=1 */
            drawMenuTop();
            drawMenuBottom();
        } else {
            drawTopScreen();
            drawBottomScreen();
        }
        C3D_FrameEnd(C3D_FRAME_SYNCDRAW);
    }

    if (g_tbuf) C2D_TextBufDelete(g_tbuf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
