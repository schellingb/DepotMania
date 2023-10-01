/*
  Depot Mania
  Copyright (C) 2023 Bernhard Schelling

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include <ZL_Application.h>
#include <ZL_Display.h>
#include <ZL_Surface.h>
#include <ZL_Signal.h>
#include <ZL_Audio.h>
#include <ZL_Font.h>
#include <ZL_Input.h>
#include <ZL_Particles.h>
#include <ZL_SynthImc.h>
#include <../Opt/chipmunk/chipmunk.cpp>
#include <vector>
#include <algorithm>

static cpSpace *space;
static cpBody *roombody, *spawnboxbody;
static cpBB room;
static int tickNextBox, tickPerBox;
static ZL_Surface srfGFX, srfPlayer, srfFloor, srfWall, srfCheck, srfStar;
static ZL_Font fntMain, fntBig;
static ZL_TextBuffer txtItems, txtScore, txtExpansion;
static int level, nitems, score, expansion, itemNextBox, boxes;
static unsigned char itemindices[16];
static unsigned char itemgoals[16];
static ZL_Rect clearrec;
static ZL_ParticleEffect particleSpark;
static bool title, gameover, win, goback;
static const ZL_Color shadow = ZLLUMA(0, .75f);
extern ZL_SynthImcTrack imcMusic;
extern TImcSongData imcDataIMCPICKUP, imcDataIMCDROP, imcDataIMCCHECK, imcDataIMCSTAR;
static ZL_Sound sndPickup, sndDrop, sndCheck, sndStar;
static std::vector<cpBody*> found;

static struct SPlayer
{
	cpBody* body;
	cpShape *mainshape, *grabshape;
	cpFloat angle = 0;
} player;

static void FixVelocityFunc(cpBody *body, cpVect gravity, cpFloat damping, cpFloat dt) {}
static void FixUpdatePositionFunc(cpBody *body, cpFloat dt) {}

static void RemoveBody(cpBody* body)
{
	while (body->constraintList) { cpConstraint* c = body->constraintList; cpSpaceRemoveConstraint(space, c); cpConstraintFree(c); }
	while (body->shapeList) { cpShape* shp = body->shapeList; cpSpaceRemoveShape(space, shp); cpShapeFree(shp); }
	cpSpaceRemoveBody(space, body);
	cpBodyFree(body);
}

static void BoxBodyUpdatePosition(cpBody *body, cpFloat dt)
{
	cpBodyUpdatePosition(body, dt);
	if (body->constraintList || body->arbiterList || body == spawnboxbody) return;

	float diffa = smod(PI2*10 + body->a + PIHALF/2, PIHALF) - PIHALF/2;
	body->a -= diffa*.1f;

	cpVect diffp = cpv(smod(body->p.x + 1000.f +.5f, 1.0f) - .5f, smod(body->p.y + 1000.f + .5f, 1.0f) - .5f);
	body->p = cpvsub(body->p, cpvmult(diffp, .05f));
}

static void Load()
{
	srfGFX = ZL_Surface("Data/gfx.png").SetTilesetClipping(4, 4).SetOrigin(ZL_Origin::Center);
	srfPlayer = ZL_Surface("Data/player.png").SetOrigin(ZL_Origin::Center).SetScale(0.02f);
	srfFloor = ZL_Surface("Data/floor.png").SetTextureRepeatMode();
	srfFloor.SetScale(1.0f/srfFloor.GetWidth(), 1.0f/srfFloor.GetHeight());
	srfWall = ZL_Surface("Data/wall.png").SetTextureRepeatMode();
	srfWall.SetScale(1.0f/srfWall.GetWidth(), 1.0f/srfFloor.GetHeight());
	srfCheck = ZL_Surface("Data/check.png").SetOrigin(ZL_Origin::Center);
	srfStar = ZL_Surface("Data/star.png").SetOrigin(ZL_Origin::Center);

	particleSpark = ZL_ParticleEffect(500, 200);
	particleSpark.AddParticleImage(ZL_Surface("Data/spark.png"), 200); //.SetColor(ZLRGB(1,.8,.1)), 200);
	particleSpark.AddBehavior(new ZL_ParticleBehavior_LinearMove(1, 0.3f));
	particleSpark.AddBehavior(new ZL_ParticleBehavior_LinearImageProperties(1, 0, 0.03f, 0.01f));

	fntMain = ZL_Font("Data/typomoderno.ttf.zip", 30.0f);
	fntBig = ZL_Font("Data/typomoderno.ttf.zip", 100.0f);
	txtItems = ZL_TextBuffer(fntMain, ZL_String::format("Items: %d/%d", nitems, COUNT_OF(itemindices)).c_str());
	txtScore = ZL_TextBuffer(fntMain, ZL_String::format("Score: %d", score).c_str());
	txtExpansion = ZL_TextBuffer(fntMain, ZL_String::format("Upgrade In: %d", expansion).c_str());

	sndPickup = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCPICKUP);
	sndDrop = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCDROP);
	sndCheck = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCCHECK);
	sndStar = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCSTAR);
	imcMusic.Play();

	title = true;
}

static void SetRoom(int _level = 0)
{
	int h = 3 + _level / 4, w = h + ((_level % 4) / 2);
	expansion += (w*h)-5;
	if (expansion <= 0) { SetRoom(_level + 1); return; }

	room = cpBBNew(-w+.5f, -h+.5f, w-.5f, h-.5f);
	if (roombody) RemoveBody(roombody);
	roombody = cpSpaceAddBody(space, cpBodyNew(999999999.f, INFINITY));
	cpBodySetVelocityUpdateFunc(roombody, FixVelocityFunc);
	cpBodySetPositionUpdateFunc(roombody, FixUpdatePositionFunc);
	cpSpaceAddShape(space, cpBoxShapeNew2(roombody, cpBBNew(room.l-1.f, room.t, room.r+1.f, room.t+1.f), 0));
	cpSpaceAddShape(space, cpBoxShapeNew2(roombody, cpBBNew(room.l-1.f, room.b-1.f, room.r+1.f, room.b), 0));
	cpSpaceAddShape(space, cpBoxShapeNew2(roombody, cpBBNew(room.l-1.f, room.b-1.f, room.l, room.t+1.f), 0));
	cpSpaceAddShape(space, cpBoxShapeNew2(roombody, cpBBNew(room.r, room.b-1.f, room.r+1.f, room.t+1.f), 0));

	srfFloor.SetColor(ZL_Color::HSVA(RAND_FACTOR,0.5f,0.3f));
	srfWall.SetColor(ZL_Color::HSVA(RAND_FACTOR,0.25f,0.3f));

	level = _level;
	nitems = ZL_Math::Min(2 + _level, (int)COUNT_OF(itemindices));
	tickPerBox = (level == 0 ? 3500 : (level == 1 ? 3000 : (level == 2 ? 2600 : 2200)));

	txtExpansion.SetText(ZL_String::format("Upgrade In: %d", expansion).c_str());
	txtItems.SetText(ZL_String::format("Items: %d/%d", nitems, COUNT_OF(itemindices)).c_str());
}

static void Init()
{
	if (space)
	{
		cpSpaceEachBody(space, [](cpBody *body, void*) { cpSpaceAddPostStepCallback(space, [](cpSpace *space, void *body, void*) { RemoveBody((cpBody*)body); }, body, NULL); }, NULL);
		cpSpaceFree(space);
	}
	roombody = spawnboxbody = NULL;
	space = cpSpaceNew();

	cpSpaceSetDamping(space, 0.0001f);
	cpSpaceSetCollisionSlop(space, .0001f); //Defaults to 0.1.

	player.body = cpSpaceAddBody(space, cpBodyNew(1, cpMomentForCircle(1, 0, 0.5f, cpvzero)));
	player.body->a = player.angle = -PIHALF;
	player.mainshape = cpSpaceAddShape(space, cpCircleShapeNew(player.body, 0.5f, cpvzero));
	player.grabshape = cpSpaceAddShape(space, cpCircleShapeNew(player.body, 0.4f, cpv(0.4f, 0)));
	cpShapeSetFilter(player.grabshape, CP_SHAPE_FILTER_NONE);

	score = expansion = 0;
	tickNextBox = 3000;
	itemNextBox = RAND_INT_RANGE(0, nitems-1);
	gameover = win = goback = false;

	memset(itemgoals, 0, sizeof(itemgoals));
	for (int i = 0; i != (int)COUNT_OF(itemindices); i++)
	{
		idxretry:
		itemindices[i] = RAND_INT_MAX(COUNT_OF(itemindices)-1);
		for (int j = 0; j != i; j++) if (itemindices[i] == itemindices[j]) goto idxretry;
	}

	SetRoom();
}

static void DrawBox(cpShape* shape, void*)
{
	if (!shape->body->userData) return;
	cpPolyShape *poly = (cpPolyShape *)shape;
	ZL_Vector off = ZLV(0.05f, -0.05f);
	ZL_Display::FillQuad(off+poly->planes[0].v0, off+poly->planes[1].v0, off+poly->planes[2].v0, off+poly->planes[3].v0, (shape->body->constraintList ? ZLRGBA(1,1,0,0.5f) : ZLLUMA(0, 0.5f)));
	srfGFX.SetTilesetIndex(itemindices[(int)shape->body->userData - 1]);
	srfGFX.DrawQuad(poly->planes[0].v0, poly->planes[1].v0, poly->planes[2].v0, poly->planes[3].v0);
	ZL_Display::DrawQuad(poly->planes[0].v0, poly->planes[1].v0, poly->planes[2].v0, poly->planes[3].v0, (shape->body->constraintList ? ZL_Color::Yellow : ZLBLACK));
	boxes++;
}

static cpBody* GetBoxAt(float x, float y, bool imperfect)
{
	cpBody* res = NULL;
	cpSpaceBBQuery(space, cpBBNewForCircle(cpv(x, y), 0.01f), CP_SHAPE_FILTER_ALL, [](cpShape *shape, void *data)
		{ if (shape->body->userData)  *((cpBody**)data) = shape->body; }, &res);
	if (!res || res->constraintList) return NULL;
	if (imperfect) return res;
	if (sabs(res->p.x - x) > 0.05f || sabs(res->p.y - y) > 0.05f) return NULL;
	float diffa = smod(PI2*10 + res->a + PIHALF/2, PIHALF) - PIHALF/2;
	if (sabs(diffa) > 0.01f) return NULL;
	return res;
}

//static int ClearBoxAndNeighbors(cpBody* box, float x, float y)
//{
//	cpDataPointer p = box->userData;
//	particleSpark.Spawn(25, box->p, 0, .5f, .5f);
//	RemoveBody(box);
//	int res = 1;
//	cpBody* nbox;
//	if ((nbox = GetBoxAt(x+1, y, true)) != NULL && nbox->userData == p) res += ClearBoxAndNeighbors(nbox, x+1, y);
//	if ((nbox = GetBoxAt(x, y+1, true)) != NULL && nbox->userData == p) res += ClearBoxAndNeighbors(nbox, x, y+1);
//	if ((nbox = GetBoxAt(x-1, y, true)) != NULL && nbox->userData == p) res += ClearBoxAndNeighbors(nbox, x-1, y);
//	if ((nbox = GetBoxAt(x, y-1, true)) != NULL && nbox->userData == p) res += ClearBoxAndNeighbors(nbox, x, y-1);
//	return res;
//}

static void FillNeighbors(cpBody* box, float x, float y)
{
	found.push_back(box);
	cpBody* nbox;
	cpDataPointer p = box->userData;
	if ((nbox = GetBoxAt(x+1, y, found.size() >= 4)) != NULL && nbox->userData == p && std::find(found.begin(), found.end(), nbox) == found.end()) FillNeighbors(nbox, x+1, y);
	if ((nbox = GetBoxAt(x, y+1, found.size() >= 4)) != NULL && nbox->userData == p && std::find(found.begin(), found.end(), nbox) == found.end()) FillNeighbors(nbox, x, y+1);
	if ((nbox = GetBoxAt(x-1, y, found.size() >= 4)) != NULL && nbox->userData == p && std::find(found.begin(), found.end(), nbox) == found.end()) FillNeighbors(nbox, x-1, y);
	if ((nbox = GetBoxAt(x, y-1, found.size() >= 4)) != NULL && nbox->userData == p && std::find(found.begin(), found.end(), nbox) == found.end()) FillNeighbors(nbox, x, y-1);
}

static void DrawTextBordered(const ZL_TextBuffer& buf, const ZL_Vector& p, scalar scale = 1, const ZL_Color& colfill = ZLWHITE, const ZL_Color& colborder = ZLBLACK, int border = 2, ZL_Origin::Type origin = ZL_Origin::Center)
{
	for (int i = 0; i < 9; i++) if (i != 4) buf.Draw(p.x+(border*((i%3)-1)), p.y+(border*((i/3)-1)), scale, scale, colborder, origin);
	buf.Draw(p.x, p.y, scale, scale, colfill, origin);
}

static void Frame()
{
	if (!title && !gameover && !win && !goback)
	{
		#ifdef ZILLALOG //DEBUG KEYS
		if (ZL_Input::Down(ZLK_F5)) SetRoom(level);
		if (ZL_Input::Down(ZLK_F6)) SetRoom(level+1);
		#endif

		if (ZL_Input::Down(ZLK_ESCAPE, true))
			goback = true;

		ZL_Vector inp = ZLV(
			((ZL_Input::Held(ZLK_RIGHT) || ZL_Input::Held(ZLK_D)) ? 1 : 0) - ((ZL_Input::Held(ZLK_LEFT) || ZL_Input::Held(ZLK_A)) ? 1 : 0),
			((ZL_Input::Held(ZLK_UP) || ZL_Input::Held(ZLK_W)) ? 1 : 0) - ((ZL_Input::Held(ZLK_DOWN) || ZL_Input::Held(ZLK_S)) ? 1 : 0)
		);

		bool grab = ZL_Input::Held(ZLK_SPACE), strafe = ZL_Input::Held(ZLK_LSHIFT) || ZL_Input::Held(ZLK_RSHIFT);
	
		static ticks_t TICKSUM = 0;
		for (TICKSUM += ZLELAPSEDTICKS; TICKSUM > 16; TICKSUM -= 16)
		{
			cpVect v = cpBodyGetVelocity(player.body);
			cpBodySetForce(player.body, cpv(inp.x*50, inp.y*50));

			if (!!inp && !strafe) player.angle = inp.GetAngle();
			float rel = ZL_Math::RelAngle(cpBodyGetAngle(player.body), player.angle);
			cpBodySetAngularVelocity(player.body, rel*10);

			if (grab && !player.body->constraintList)
			{
				cpShapeSetFilter(player.grabshape, CP_SHAPE_FILTER_ALL);
				cpSpaceShapeQuery(space, player.grabshape, [](cpShape *shape, cpContactPointSet *points, void *data)
				{
					if (!shape->body->userData) return;

					//cpVect mid = cpvlerp(points->points[0].pointA, points->points[0].pointB, 0.5f);
					cpVect off = cpvmult(cpvperp(cpvnormalize(cpvsub(shape->body->p, player.body->p))), 0.1f);
					cpConstraint * c1 = cpPinJointNew(player.body, shape->body, cpvadd(cpv(0.4f, 0), cpBodyWorldToLocal(player.body, cpvadd(player.body->p, off))), cpBodyWorldToLocal(shape->body, cpvadd(shape->body->p, off)));
					off = cpvneg(off);
					cpConstraint * c2 = cpPinJointNew(player.body, shape->body, cpvadd(cpv(0.4f, 0), cpBodyWorldToLocal(player.body, cpvadd(player.body->p, off))), cpBodyWorldToLocal(shape->body, cpvadd(shape->body->p, off)));

					cpSpaceAddPostStepCallback(space, [](cpSpace *space, void *key, void *data) { cpSpaceAddConstraint(space, (cpConstraint *)key); }, c1, NULL);
					cpSpaceAddPostStepCallback(space, [](cpSpace *space, void *key, void *data) { cpSpaceAddConstraint(space, (cpConstraint *)key); }, c2, NULL);

				}, NULL);
				cpShapeSetFilter(player.grabshape, CP_SHAPE_FILTER_NONE);
				if (player.body->constraintList) sndPickup.Play();
			}
			if (!grab && player.body->constraintList)
			{
				while (cpConstraint* c = player.body->constraintList) { cpSpaceRemoveConstraint(space, c); cpConstraintFree(c); }
				sndDrop.Play();
			}

			tickNextBox -= 16;
			if (!spawnboxbody && tickNextBox <= 0)
			{
				spawnboxbody = cpSpaceAddBody(space, cpBodyNew(0.1f, cpMomentForBox(0.1f, 1, 1)));
				cpBodySetUserData(spawnboxbody, (cpDataPointer)(itemNextBox+1));
				cpBodySetPosition(spawnboxbody, cpv(0, room.b+.01f));
				cpSpaceAddShape(space, cpBoxShapeNew(spawnboxbody, .01f, .01f, 0.01f));
			}
			if (spawnboxbody)
			{
				float f = ZL_Math::Min(-tickNextBox / 1000.0f, 1.0f);

				cpBB bb = cpBBNewForCircle(cpvzero, (.01f + .9f * f) * 0.5f);
				cpVect verts[] = { {bb.r, bb.b}, {bb.r, bb.t}, {bb.l, bb.t}, {bb.l, bb.b}, };
				cpPolyShapeSetVerts(spawnboxbody->shapeList, 4, verts, cpTransformIdentity);

				if (f == 1.0)
				{
					cpBodySetPositionUpdateFunc(spawnboxbody, BoxBodyUpdatePosition);
					spawnboxbody = NULL;
					tickNextBox = tickPerBox;
					itemNextBox = RAND_INT_RANGE(0, nitems-1);
				}
			}

			cpSpaceStep(space, s(16.0/1000.0));
		}

		for (float y = room.b + .5f; y < room.t; y++)
		{
			for (float x = room.l + .5f; x < room.r; x++)
			{
				cpBody *box = GetBoxAt(x, y, false);
				if (!box) continue;

				//cpDataPointer p = box->userData;
				//cpBody *nbox;
				//if ((nbox = GetBoxAt(x + 1, y    , false)) == NULL || nbox->userData != p) continue;
				//if ((nbox = GetBoxAt(x    , y + 1, false)) == NULL || nbox->userData != p) continue;
				//if ((nbox = GetBoxAt(x + 1, y + 1, false)) == NULL || nbox->userData != p) continue;
				//int item = (int)box->userData - 1;
				//int area = ClearBoxAndNeighbors(box, x, y);

				found.clear();
				FillNeighbors(box, x, y);
				if (found.size() < 4) continue;
				int area = (int)found.size();
				int item = (int)box->userData - 1;
				for (cpBody* it : found)
				{
					particleSpark.Spawn(25, it->p, 0, .5f, .5f);
					RemoveBody(it);
				}

				bool newclear = (itemgoals[item] == 0);
				itemgoals[item] |= ((area >= 6) ? 3 : 1);
				((area >= 6) ? sndStar : sndCheck).Play();
				if (newclear)
				{
					bool allclear = true;
					for (int i = 0; i != COUNT_OF(itemindices); i++)
						if (!itemgoals[i]) { allclear = false; break; }
					if (allclear) win = true;
				}

				score += area;
				expansion -= area;
				txtScore = ZL_TextBuffer(fntMain, ZL_String::format("Score: %d", score).c_str());
				txtExpansion = ZL_TextBuffer(fntMain, ZL_String::format("Upgrade In: %d", expansion).c_str());
				if (expansion <= 0) SetRoom(level + 1);
			}
		}
	}

	static float lastsz = 0;
	float ar = ZL_Display::Width / ZL_Display::Height, sz = room.r + 1.0f;
	if (!lastsz) lastsz = sz;
	sz = lastsz = ZL_Math::Lerp(lastsz, sz, .01f);
	ZL_Display::PushOrtho(-sz*ar, sz*ar, -sz, sz);

	srfFloor.DrawTo(room.l, room.b, room.r, room.t);

	if (title)
	{
		if (ZL_Input::Down(ZLK_ESCAPE))
			ZL_Application::Quit();

		if (ZL_Input::Down(ZLK_RETURN) || ZL_Input::Down(ZLK_RETURN2) || ZL_Input::Down(ZLK_SPACE))
		{
			Init();
			title = false;
		}

		srfWall.DrawTo(room.l - 100.f, room.b - 100.f, room.r + 100.f, room.t + 100.f);
		ZL_Display::PopOrtho();

		static ZL_TextBuffer txt(fntBig, "Depot");
		txt.Draw(ZLHALFW-200+18, ZLHALFH+200-18, 1.5f, shadow, ZL_Origin::Center);
		DrawTextBordered(txt, ZLV(ZLHALFW-200, ZLHALFH+200), 1.5f, ZLWHITE, ZLBLACK, 5);

		static ZL_TextBuffer txt2(fntBig, "Mania");
		ZL_Display::PushMatrix();
		ZL_Display::Translate(ZLHALFW+200, ZLHALFH+200);
		ZL_Display::Rotate(ssin(ZLSECONDS*20)*0.1f);
		txt2.Draw(0+18, 0-18, 1.5f, shadow, ZL_Origin::Center);
		DrawTextBordered(txt2, ZLV(0, 0), 1.5f, ZLWHITE, ZLBLACK, 5);
		ZL_Display::PopMatrix();

		static ZL_TextBuffer txt3(fntBig, "Connect 4 items of the same type to clear them");
		DrawTextBordered(txt3, ZLV(ZLHALFW, ZLHALFH-20), .4f, ZLWHITE, ZLBLACK, 3);
		static ZL_TextBuffer txt4(fntBig, "Connect 6 for bonus stars, clear all items once to win");
		DrawTextBordered(txt4, ZLV(ZLHALFW, ZLHALFH-70), .4f, ZLWHITE, ZLBLACK, 3);

		static ZL_TextBuffer txt5(fntBig, "Use Arrows or WASD to Move");
		DrawTextBordered(txt5, ZLV(ZLHALFW, ZLHALFH-160), .5f, ZLWHITE, ZLBLACK, 3);
		static ZL_TextBuffer txt6(fntBig, "Hold Space to Carry Items");
		DrawTextBordered(txt6, ZLV(ZLHALFW, ZLHALFH-215), .5f, ZLWHITE, ZLBLACK, 3);
		static ZL_TextBuffer txt7(fntBig, "Hold Shift to Strafe (Move without Turning)");
		DrawTextBordered(txt7, ZLV(ZLHALFW, ZLHALFH-280), .5f, ZLWHITE, ZLBLACK, 3);

		static ZL_TextBuffer txt8(fntMain, "(C) 2023 by Bernhard Schelling");
		DrawTextBordered(txt8, ZLV(ZLHALFW, 20), .7f, ZLWHITE*.7f, ZLBLACK, 2);
		return;
	}

	boxes = 0;
	cpSpaceEachShape(space, DrawBox, NULL);
	if (boxes > (room.r - room.l)*(room.t - room.b)-1) gameover = 1;

	srfPlayer.Draw(ZLV(0.1,-0.1) + player.body->p, player.body->a, ZLLUMA(0, 0.75));
	srfPlayer.Draw(player.body->p, player.body->a);
	ZL_Display::FillGradient(room.l, room.t - .3f, room.r, room.t, ZLBLACK, ZLBLACK, ZLTRANSPARENT, ZLTRANSPARENT);
	ZL_Display::FillGradient(room.l, room.b, room.l + .3f, room.t, ZLBLACK, ZLTRANSPARENT, ZLBLACK, ZLTRANSPARENT);
	ZL_Display::FillGradient(room.l, room.b, room.r, room.b + .1f, ZLTRANSPARENT, ZLTRANSPARENT, ZLBLACK, ZLBLACK);
	ZL_Display::FillGradient(room.r - .1f, room.b, room.r, room.t, ZLTRANSPARENT, ZLBLACK, ZLTRANSPARENT, ZLBLACK);

	srfWall.DrawTo(room.l - 100.f, room.t, room.r + 100.f, room.t + 100.f);
	srfWall.DrawTo(room.l - 100.f, room.b - 100.f, room.r + 100.f, room.b);
	srfWall.DrawTo(room.l - 100.f, room.b, room.l, room.t);
	srfWall.DrawTo(room.r, room.b, room.r + 100.f, room.t);
	if (!spawnboxbody && tickNextBox)
	{
		float f = (float)(tickNextBox) / tickPerBox;
		if (f <= 1.0f)
			srfGFX.SetTilesetIndex(itemindices[itemNextBox]).Draw(0, room.b-0.1f-0.4f*f, 0.02f*(f+0.3f), 0.02f*(f+0.3f), ZLLUMA(0.8,0.8));
	}
	ZL_Display::FillGradient(-0.5f, room.b-0.3f, 0.5f, room.b, ZLBLACK, ZLBLACK, ZLTRANSPARENT, ZLTRANSPARENT);

	particleSpark.Draw();

	#ifdef ZILLALOG //DEBUG DRAW
	if (ZL_Input::Held(ZLK_RSHIFT)) { void DebugDrawConstraint(cpConstraint*, void*); cpSpaceEachConstraint(space, DebugDrawConstraint, NULL); }
	if (ZL_Input::Held(ZLK_RSHIFT)) { void DebugDrawShape(cpShape*,void*); cpSpaceEachShape(space, DebugDrawShape, NULL); }
	#endif

	ZL_Display::PopOrtho();
	txtItems.Draw(10+3, ZLFROMH(40)-3, shadow);
	txtItems.Draw(10, ZLFROMH(40));
	txtScore.Draw(10+3, ZLFROMH(80)-3, shadow);
	txtScore.Draw(10, ZLFROMH(80));
	txtExpansion.Draw(10+3, ZLFROMH(120)-3, shadow);
	txtExpansion.Draw(10, ZLFROMH(120));
	for (int i = 0; i != nitems; i++)
	{
		srfGFX.Draw(158.0f + i * 40.f+3, ZLFROMH(32)-3, shadow);
		srfGFX.SetTilesetIndex(itemindices[i]).Draw(158.0f + i * 40.f, ZLFROMH(32));
		if (itemgoals[i] & 1) srfCheck.Draw(158.0f + i * 40.f, ZLFROMH(32));
		if (itemgoals[i] & 2) srfStar.Draw(158.0f + i * 40.f, ZLFROMH(32));
	}

	if (gameover)
	{
		if (ZL_Input::Down(ZLK_ESCAPE)) title = true;
		static ZL_TextBuffer txt(fntBig, "Game Over");
		DrawTextBordered(txt, ZLCENTER, 1.5f, ZLWHITE, ZLBLACK, 5);
		static ZL_TextBuffer txt2(fntBig, "Press ESC to Return to Title");
		DrawTextBordered(txt2, ZLV(ZLHALFW, ZLHALFH-160), .5f, ZLWHITE, ZLBLACK, 3);
	}
	if (win)
	{
		if (ZL_Input::Down(ZLK_ESCAPE)) win = false;
		static ZL_TextBuffer txt(fntBig, "You Win! Congratulation!");
		DrawTextBordered(txt, ZLCENTER, 1.5f, ZLWHITE, ZLBLACK, 5);
		static ZL_TextBuffer txt2(fntBig, "Thank you for playing");
		DrawTextBordered(txt2, ZLV(ZLHALFW, ZLHALFH-110), .5f, ZLWHITE, ZLBLACK, 3);
		static ZL_TextBuffer txt3(fntBig, "Press ESC to Continue Playing");
		DrawTextBordered(txt3, ZLV(ZLHALFW, ZLHALFH-190), .5f, ZLWHITE, ZLBLACK, 3);
	}
	if (goback)
	{
		if (ZL_Input::Down(ZLK_ESCAPE)) title = true;
		if (ZL_Input::Down(ZLK_SPACE)) goback = false;
		static ZL_TextBuffer txt(fntBig, "Paused");
		DrawTextBordered(txt, ZLCENTER, 1.5f, ZLWHITE, ZLBLACK, 5);
		static ZL_TextBuffer txt2(fntBig, "Press ESC to Return to Title");
		DrawTextBordered(txt2, ZLV(ZLHALFW, ZLHALFH-110), .5f, ZLWHITE, ZLBLACK, 3);
		static ZL_TextBuffer txt3(fntBig, "Press Space to Continue Playing");
		DrawTextBordered(txt3, ZLV(ZLHALFW, ZLHALFH-190), .5f, ZLWHITE, ZLBLACK, 3);
	}
}

static struct sDepotMania : public ZL_Application
{
	sDepotMania() : ZL_Application(60) { }

	virtual void Load(int argc, char *argv[])
	{
		if (!ZL_Application::LoadReleaseDesktopDataBundle()) return;
		if (!ZL_Display::Init("Depot Mania", 1280, 720, ZL_DISPLAY_ALLOWRESIZEHORIZONTAL)) return;
		ZL_Display::ClearFill(ZL_Color::White);
		ZL_Display::SetAA(true);
		ZL_Audio::Init();
		ZL_Input::Init();
		::Load();
		::Init();
	}
	virtual void AfterFrame()
	{
		::Frame();
	}
} DepotMania;

#ifdef ZILLALOG //DEBUG DRAW
void DebugDrawShape(cpShape *shape, void*)
{
	switch (shape->klass->type)
	{
		case CP_CIRCLE_SHAPE: {
			cpCircleShape *circle = (cpCircleShape *)shape;
			ZL_Display::DrawCircle(circle->tc, circle->r, ZL_Color::Green);
			break; }
		case CP_SEGMENT_SHAPE: {
			cpSegmentShape *seg = (cpSegmentShape *)shape;
			cpVect vw = cpvclamp(cpvperp(cpvsub(seg->tb, seg->ta)), seg->r);
			ZL_Display::DrawQuad(seg->ta.x + vw.x, seg->ta.y + vw.y, seg->tb.x + vw.x, seg->tb.y + vw.y, seg->tb.x - vw.x, seg->tb.y - vw.y, seg->ta.x - vw.x, seg->ta.y - vw.y, ZLRGBA(0,1,1,.35), ZLRGBA(1,1,0,.35));
			ZL_Display::DrawCircle(seg->ta, seg->r, ZLRGBA(0,1,1,.35), ZLRGBA(1,1,0,.35));
			ZL_Display::DrawCircle(seg->tb, seg->r, ZLRGBA(0,1,1,.35), ZLRGBA(1,1,0,.35));
			break; }
		case CP_POLY_SHAPE: {
			cpPolyShape *poly = (cpPolyShape *)shape;
			{for (int i = 1; i < poly->count; i++) ZL_Display::DrawLine(poly->planes[i-1].v0, poly->planes[i].v0, ZLWHITE);}
			ZL_Display::DrawLine(poly->planes[poly->count-1].v0, poly->planes[0].v0, ZLWHITE);
			break; }
	}
	if (shape->body == roombody) return;
	ZL_Display::FillCircle(cpBodyGetPosition(shape->body), .1f, ZL_Color::Red);
	ZL_Display::DrawLine(cpBodyGetPosition(shape->body), (ZL_Vector&)cpBodyGetPosition(shape->body) + ZLV(cpBodyGetAngularVelocity(shape->body)*-2, 0), ZLRGB(1,0,0));
	ZL_Display::DrawLine(cpBodyGetPosition(shape->body), (ZL_Vector&)cpBodyGetPosition(shape->body) + ZL_Vector::FromAngle(cpBodyGetAngle(shape->body))*2, ZLRGB(1,1,0));
}

void DebugDrawConstraint(cpConstraint *constraint, void *data)
{
	cpBody *body_a = constraint->a, *body_b = constraint->b;

	if(cpConstraintIsPinJoint(constraint) || cpConstraintIsSlideJoint(constraint))
	{
		cpPinJoint *joint = (cpPinJoint *)constraint;
		cpVect a = (cpBodyGetType(body_a) == CP_BODY_TYPE_KINEMATIC ? body_a->p : cpTransformPoint(body_a->transform, joint->anchorA));
		cpVect b = (cpBodyGetType(body_b) == CP_BODY_TYPE_KINEMATIC ? body_b->p : cpTransformPoint(body_b->transform, joint->anchorB));
		ZL_Display::DrawLine(a.x, a.y, b.x, b.y, ZL_Color::Magenta);
	}
	else if (cpConstraintIsPivotJoint(constraint))
	{
		cpPivotJoint *joint = (cpPivotJoint *)constraint;
		cpVect a = (cpBodyGetType(body_a) == CP_BODY_TYPE_KINEMATIC ? body_a->p : cpTransformPoint(body_a->transform, joint->anchorA));
		cpVect b = (cpBodyGetType(body_b) == CP_BODY_TYPE_KINEMATIC ? body_b->p : cpTransformPoint(body_b->transform, joint->anchorB));
		ZL_Display::DrawLine(a.x, a.y, b.x, b.y, ZL_Color::Magenta);
		ZL_Display::FillCircle(a, .1f, ZL_Color::Magenta);
		ZL_Display::FillCircle(b, .1f, ZL_Color::Magenta);
	}
	else if (cpConstraintIsRotaryLimitJoint(constraint))
	{
		cpRotaryLimitJoint *joint = (cpRotaryLimitJoint *)constraint;
		cpVect a = cpTransformPoint(body_a->transform, cpvzero);
		cpVect b = cpvadd(a, cpvmult(cpvforangle(joint->min), 40));
		cpVect c = cpvadd(a, cpvmult(cpvforangle(joint->max), 40));
		ZL_Display::DrawLine(a.x, a.y, b.x, b.y, ZL_Color::Magenta);
		ZL_Display::DrawLine(a.x, a.y, c.x, c.y, ZL_Color::Magenta);
	}
}
#endif
static const unsigned int IMCMUSIC_OrderTable[] = {
	0x011000001, 0x012000002, 0x011000002, 0x012000001, 0x023000003, 0x013000003, 0x023000004, 0x024000005,
};
static const unsigned char IMCMUSIC_PatternData[] = {
	0x50, 0, 0x52, 0, 0x50, 0, 0x52, 0, 0x50, 0, 0x52, 0, 0x50, 0, 0x52, 0,
	0x54, 0, 0x54, 0, 0x55, 0, 0x54, 0, 0x57, 0, 0x57, 0, 0x44, 0, 0x44, 0,
	0x57, 0x57, 0, 0, 0x59, 0x59, 0, 0, 0x55, 0x55, 0, 0, 0x54, 0x52, 0x50, 0,
	0x50, 0, 0, 0, 0x59, 0, 0, 0, 0x54, 0, 0, 0, 0x50, 0, 0, 0,
	0x49, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x54, 0, 0x54, 0, 0x55, 0, 0x55, 0, 0x47, 0, 0x47, 0, 0x45, 0, 0, 0,
	0x40, 0, 0, 0, 0, 0, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0,
	0x50, 0, 0, 0, 0x42, 0, 0, 0, 0x50, 0, 0, 0, 0x42, 0, 0, 0,
	0x59, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCMUSIC_PatternLookupTable[] = { 0, 5, 5, 5, 5, 5, 5, 9, };
static const TImcSongEnvelope IMCMUSIC_EnvList[] = {
	{ 0, 256, 87, 8, 16, 255, true, 255, },
	{ 196, 256, 29, 8, 16, 255, true, 255, },
	{ 0, 256, 173, 8, 16, 255, true, 255, },
	{ 196, 256, 31, 8, 16, 255, true, 255, },
	{ 0, 128, 1046, 8, 16, 255, true, 255, },
	{ 0, 256, 280, 1, 22, 6, true, 255, },
	{ 50, 100, 15, 8, 255, 255, false, 0, },
	{ 0, 256, 87, 8, 16, 255, true, 255, },
	{ 0, 256, 5, 8, 16, 255, false, 255, },
};
static TImcSongEnvelopeCounter IMCMUSIC_EnvCounterList[] = {
	{ -1, -1, 256 }, { 0, 0, 256 }, { 1, 0, 256 }, { 2, 0, 256 },
	{ 3, 0, 256 }, { 0, 0, 256 }, { 4, 0, 128 }, { 5, 6, 177 },
	{ 6, 6, 100 }, { 7, 7, 256 }, { 8, 7, 256 },
};
static const TImcSongOscillator IMCMUSIC_OscillatorList[] = {
	{ 5, 0, IMCSONGOSCTYPE_SINE, 0, -1, 98, 1, 2 },
	{ 6, 0, IMCSONGOSCTYPE_SINE, 0, -1, 98, 3, 4 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 0, -1, 0, 5, 6 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 6, 0, IMCSONGOSCTYPE_SINE, 6, -1, 216, 0, 0 },
	{ 6, 253, IMCSONGOSCTYPE_SINE, 6, -1, 60, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SAW, 7, -1, 100, 0, 0 },
	{ 9, 0, IMCSONGOSCTYPE_SINE, 7, 10, 100, 0, 0 },
};
static const TImcSongEffect IMCMUSIC_EffectList[] = {
	{ 6350, 913, 1, 0, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 0 },
	{ 44, 0, 16536, 0, IMCSONGEFFECTTYPE_DELAY, 0, 0 },
	{ 3429, 946, 1, 6, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 0 },
	{ 0, 0, 101, 6, IMCSONGEFFECTTYPE_FLANGE, 8, 0 },
	{ 16, 173, 1, 6, IMCSONGEFFECTTYPE_RESONANCE, 0, 0 },
	{ 117, 0, 11024, 7, IMCSONGEFFECTTYPE_DELAY, 0, 0 },
	{ 255, 0, 1, 7, IMCSONGEFFECTTYPE_HIGHPASS, 10, 0 },
};
static unsigned char IMCMUSIC_ChannelVol[8] = { 225, 100, 100, 100, 100, 100, 66, 71 };
static const unsigned char IMCMUSIC_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 7, 9 };
static const bool IMCMUSIC_ChannelStopNote[8] = { true, false, false, false, false, false, false, true };
TImcSongData imcDataIMCMUSIC = {
	/*LEN*/ 0x8, /*ROWLENSAMPLES*/ 5512, /*ENVLISTSIZE*/ 9, /*ENVCOUNTERLISTSIZE*/ 11, /*OSCLISTSIZE*/ 12, /*EFFECTLISTSIZE*/ 7, /*VOL*/ 15,
	IMCMUSIC_OrderTable, IMCMUSIC_PatternData, IMCMUSIC_PatternLookupTable, IMCMUSIC_EnvList, IMCMUSIC_EnvCounterList, IMCMUSIC_OscillatorList, IMCMUSIC_EffectList,
	IMCMUSIC_ChannelVol, IMCMUSIC_ChannelEnvCounter, IMCMUSIC_ChannelStopNote };
ZL_SynthImcTrack imcMusic(&imcDataIMCMUSIC);

static const unsigned int IMCPICKUP_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCPICKUP_PatternData[] = {
	0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCPICKUP_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCPICKUP_EnvList[] = {
	{ 0, 256, 65, 8, 16, 4, true, 255, },
	{ 0, 256, 64, 8, 16, 255, true, 255, },
	{ 100, 200, 87, 25, 15, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCPICKUP_EnvCounterList[] = {
	{ 0, 0, 256 }, { 1, 0, 256 }, { 2, 0, 100 }, { -1, -1, 256 },
};
static const TImcSongOscillator IMCPICKUP_OscillatorList[] = {
	{ 8, 0, IMCSONGOSCTYPE_SINE, 0, -1, 124, 1, 2 },
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 0, 0, 50, 3, 3 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
static unsigned char IMCPICKUP_ChannelVol[8] = { 100, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCPICKUP_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCPICKUP_ChannelStopNote[8] = { false, false, false, false, false, false, false, false };
TImcSongData imcDataIMCPICKUP = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 5512, /*ENVLISTSIZE*/ 3, /*ENVCOUNTERLISTSIZE*/ 4, /*OSCLISTSIZE*/ 9, /*EFFECTLISTSIZE*/ 0, /*VOL*/ 120,
	IMCPICKUP_OrderTable, IMCPICKUP_PatternData, IMCPICKUP_PatternLookupTable, IMCPICKUP_EnvList, IMCPICKUP_EnvCounterList, IMCPICKUP_OscillatorList, NULL,
	IMCPICKUP_ChannelVol, IMCPICKUP_ChannelEnvCounter, IMCPICKUP_ChannelStopNote };


static const unsigned int IMCDROP_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCDROP_PatternData[] = {
	0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCDROP_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCDROP_EnvList[] = {
	{ 0, 256, 65, 8, 16, 4, true, 255, },
	{ 0, 256, 64, 8, 16, 255, true, 255, },
	{ 100, 200, 87, 8, 15, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCDROP_EnvCounterList[] = {
	{ 0, 0, 256 }, { 1, 0, 256 }, { 2, 0, 200 }, { -1, -1, 256 },
};
static const TImcSongOscillator IMCDROP_OscillatorList[] = {
	{ 8, 0, IMCSONGOSCTYPE_SINE, 0, -1, 124, 1, 2 },
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 0, 0, 50, 3, 3 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
static unsigned char IMCDROP_ChannelVol[8] = { 100, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCDROP_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCDROP_ChannelStopNote[8] = { false, false, false, false, false, false, false, false };
TImcSongData imcDataIMCDROP = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 5512, /*ENVLISTSIZE*/ 3, /*ENVCOUNTERLISTSIZE*/ 4, /*OSCLISTSIZE*/ 9, /*EFFECTLISTSIZE*/ 0, /*VOL*/ 120,
	IMCDROP_OrderTable, IMCDROP_PatternData, IMCDROP_PatternLookupTable, IMCDROP_EnvList, IMCDROP_EnvCounterList, IMCDROP_OscillatorList, NULL,
	IMCDROP_ChannelVol, IMCDROP_ChannelEnvCounter, IMCDROP_ChannelStopNote };

static const unsigned int IMCCHECK_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCCHECK_PatternData[] = {
	0x50, 0x52, 0x54, 0x55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCCHECK_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCCHECK_EnvList[] = {
	{ 0, 150, 144, 1, 23, 255, true, 255, },
	{ 0, 256, 699, 8, 16, 255, true, 255, },
	{ 0, 256, 172, 8, 16, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCCHECK_EnvCounterList[] = {
	{ 0, 0, 92 }, { 1, 0, 256 }, { -1, -1, 256 }, { 2, 0, 256 },
};
static const TImcSongOscillator IMCCHECK_OscillatorList[] = {
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 0, -1, 230, 1, 2 },
	{ 8, 200, IMCSONGOSCTYPE_SINE, 0, -1, 124, 3, 2 },
	{ 10, 106, IMCSONGOSCTYPE_SQUARE, 0, -1, 82, 2, 2 },
	{ 9, 15, IMCSONGOSCTYPE_SQUARE, 0, 2, 52, 2, 2 },
};
static const TImcSongEffect IMCCHECK_EffectList[] = {
	{ 120, 0, 1, 0, IMCSONGEFFECTTYPE_LOWPASS, 2, 0 },
	{ 15748, 530, 1, 0, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 2 },
};
static unsigned char IMCCHECK_ChannelVol[8] = { 84, 84, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCCHECK_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCCHECK_ChannelStopNote[8] = { true, true, false, false, false, false, false, false };
TImcSongData imcDataIMCCHECK = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 2594, /*ENVLISTSIZE*/ 3, /*ENVCOUNTERLISTSIZE*/ 4, /*OSCLISTSIZE*/ 4, /*EFFECTLISTSIZE*/ 2, /*VOL*/ 63,
	IMCCHECK_OrderTable, IMCCHECK_PatternData, IMCCHECK_PatternLookupTable, IMCCHECK_EnvList, IMCCHECK_EnvCounterList, IMCCHECK_OscillatorList, IMCCHECK_EffectList,
	IMCCHECK_ChannelVol, IMCCHECK_ChannelEnvCounter, IMCCHECK_ChannelStopNote };

static const unsigned int IMCSTAR_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCSTAR_PatternData[] = {
	0x54, 0x52, 0x55, 0x57, 0x55, 0x59, 0x5B, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCSTAR_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCSTAR_EnvList[] = {
	{ 0, 150, 144, 1, 23, 255, true, 255, },
	{ 0, 256, 699, 8, 16, 255, true, 255, },
	{ 0, 256, 172, 8, 16, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCSTAR_EnvCounterList[] = {
	{ 0, 0, 92 }, { 1, 0, 256 }, { -1, -1, 256 }, { 2, 0, 256 },
};
static const TImcSongOscillator IMCSTAR_OscillatorList[] = {
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 0, -1, 230, 1, 2 },
	{ 8, 200, IMCSONGOSCTYPE_SINE, 0, -1, 124, 3, 2 },
	{ 10, 106, IMCSONGOSCTYPE_SQUARE, 0, -1, 82, 2, 2 },
	{ 9, 15, IMCSONGOSCTYPE_SQUARE, 0, 2, 52, 2, 2 },
};
static const TImcSongEffect IMCSTAR_EffectList[] = {
	{ 120, 0, 1, 0, IMCSONGEFFECTTYPE_LOWPASS, 2, 0 },
	{ 15748, 530, 1, 0, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 2 },
};
static unsigned char IMCSTAR_ChannelVol[8] = { 84, 84, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCSTAR_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCSTAR_ChannelStopNote[8] = { true, true, false, false, false, false, false, false };
TImcSongData imcDataIMCSTAR = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 2594, /*ENVLISTSIZE*/ 3, /*ENVCOUNTERLISTSIZE*/ 4, /*OSCLISTSIZE*/ 4, /*EFFECTLISTSIZE*/ 2, /*VOL*/ 63,
	IMCSTAR_OrderTable, IMCSTAR_PatternData, IMCSTAR_PatternLookupTable, IMCSTAR_EnvList, IMCSTAR_EnvCounterList, IMCSTAR_OscillatorList, IMCSTAR_EffectList,
	IMCSTAR_ChannelVol, IMCSTAR_ChannelEnvCounter, IMCSTAR_ChannelStopNote };
