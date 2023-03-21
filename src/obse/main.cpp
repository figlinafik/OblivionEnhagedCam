
#include "obse/PluginAPI.h"
#include "obse/CommandTable.h"

#include "obse/GameAPI.h"
#include "obse/GameData.h"
#include "obse/GameForms.h"
#include "obse/GameMenus.h"
#include "obse/GameObjects.h"
#include "obse/GameProcess.h"
#include "obse/NiAPI.h"
#include "obse/NiControllers.h"
#include "obse/NiNodes.h"
#include "obse/NiObjects.h"
#include "obse/NiTypes.h"

#include "obse_common/SafeWrite.h"
#include "obse/Utilities.h"

#include <windows.h>

#include "matrix.h"


PluginHandle g_pluginHandle = kPluginHandle_Invalid;

//IDebugLog		gLog("OBSE_EnhancedCamera.log");

char inipath[] = "Data\\obse\\plugins\\OBSE_EnhancedCamera.ini";

// ini settings
static int g_bEnableFirstPersonBody = 0;
static int g_bEnableHeadBob = 0;
static int g_bFirstPersonShadows = 0;
static int g_bVisibleArmsWeaponSheathed = 0;
static int g_bHideShieldWeaponSheathed = 0;
static int g_bFirstPersonSitting = 0;
static int g_bFirstPersonKnockout = 0;
static int g_bFirstPersonDeath = 0;
static int g_bFirstPersonModAnim = 0;
static int g_bFirstPersonAnimCameraRotation = 0;
static int g_bPreventChaseCamDistanceReset = 0;
static int g_bEnableHeadFirstPerson = 0;
static int g_bHorseFirstPersonBody = 0;
static int g_bUseThirdPersonArms = 0;
static int g_bRotateThirdPersonArms = 0;
static float g_fSittingMaxLookingDownOverride = 0.0;
static float g_fMountedMaxLookingDownOverride = 0.0;
static float g_fVanityModeForceDefaultOverride = 0.0;
static float g_fCameraPosX = 0.0;
static float g_fCameraPosY = 0.0;
static float g_fCameraPosZ = 0.0;
static float g_fHorseMountCameraPosY = 0.0;
static float g_fVampireFeedCameraPosY = 0.0;
static float g_fCameraZOffset = 0.0;
static float g_fVampireFeedXYOffset = 0.0;
static float g_fVampireFeedZOffset = 0.0;

// Stores if the game is in 1st or 3rd person
static int g_isThirdPerson = 0;

// This should be set when the game force switches to 3rd person (sitting anim, knockout)
// if set the camera should be moved in front of the player's face
static int g_isForceThirdPerson = 0;

// Should not enable visible body while riding a horse
// (can sometimes see yourself without a head/arms)
static int g_horseToggle = 0;

// Used to detect when a mod switches to third person to play an animation
static int g_idlePlaying = 0;
static int g_checkIdle = 0;

// Used to detect if player is in dialogue
static int g_inDialogueMenu = 0;

// Saves the 3rd person camera zoom
static float g_saveThirdPersonZoom = 0;

// Set to disable applying collisions to the animation
static float g_animSkipCollisions = 0;

// Set to disable particle system updates in the animation
static float g_animSkipParticleUpdate = 0;


// Pointer to the First Person camera node stored in Oblivion
// See: GameObjects.cpp, g_1stPersonCameraNode
static NiNode ** g_FirstPersonCameraNode = (NiNode **)0x00B3BB0C;


// Calls Oblivion's built-in procedure for scanning nif trees - NiAVObject::Unk_16()
NiNode * FindNode(NiNode * node, const char * name) {
	_asm
	{
		mov edx, name
		mov ecx, node
		push edx
		mov eax, dword ptr [ecx]
		mov edx, dword ptr [eax+0x58]
		call edx // This automatically sets the return value
	}
}


// Returns if an actor is currently vampire feeding
bool IsVampireFeeding(Actor * act) {
	_asm
	{
		mov ecx, act
		mov edx, [ecx]
		mov eax, [edx+0x358]
		call eax // HasVampireFeedPackage(), sets return value
	}
}

// Applies MagicShaderHitEffect to player
bool ApplyMagicEffectShader(MagicShaderHitEffect * shader) {
	_asm
	{
		mov ecx, shader
		mov edx, [ecx]
		mov eax, [edx+0x68]
		call eax // Applies the shader, sets return value
	}
}

// Returns true if dialog menu is open
bool InDialogMenu() {
	return g_inDialogueMenu;
}


// Gets the actor's movement flags
int GetMovementFlags(Actor * actor) {
	BaseProcess * proc = actor->process;
	return proc->GetMovementFlags();
}

// Returns true if the actor is sneaking
bool IsSneaking(Actor * actor) {
	UInt32 moveFlags = GetMovementFlags(actor);
	bool isSneaking = (moveFlags & 0x00000400) != 0;
	bool isSwimming = (moveFlags & 0x00000800) != 0;
	return isSneaking && !isSwimming;
}

// Returns true if the actor is swimming
bool IsSwimming(Actor * actor) {
	UInt32 moveFlags = GetMovementFlags(actor);
	bool isSwimming = (moveFlags & 0x00000800) != 0;
	return isSwimming;
}


const UInt32 kIsIdlePlayingCall = 0x00472EA0;

// Returns true if a special idle is playing on the player
bool IsIdlePlaying(int firstPerson = 0) {
	ActorAnimData * animData;
	if (firstPerson == 0) {
		HighProcess * proc = static_cast<HighProcess *>((*g_thePlayer)->process);
		animData = proc->animData;
	} else {
		animData = (*g_thePlayer)->firstPersonAnimData;
	}
	_asm
	{
		mov ecx, animData
		call kIsIdlePlayingCall
		not al
		and al, 0x1
	}
}

// Returns true if the player is casting
bool IsCastingSpells() {
	HighProcess * proc = static_cast<HighProcess *>((*g_thePlayer)->process);
	ActorAnimData * animData = proc->animData;
	int isCasting = 0;

	if (animData) {
		if (animData->FindAnimInRange(TESAnimGroup::kAnimGroup_CastSelf, TESAnimGroup::kAnimGroup_CastTargetAlt)) {
			isCasting = 1;
		}
	}
	return isCasting;
}


// Returns true if the player is equipping/unequipping a weapon
bool IsEquippingWeapon() {
	HighProcess * proc = static_cast<HighProcess *>((*g_thePlayer)->process);
	ActorAnimData * animData = proc->animData;
	int isEquipping = 0;

	if (animData) {
		if (animData->FindAnimInRange(TESAnimGroup::kAnimGroup_Equip, TESAnimGroup::kAnimGroup_Unequip)) {
			isEquipping = 1;
		}
	}
	return isEquipping;
}


// Sets and resets scale of 3rd person skeleton nodes
// Must also call ThisStdCall(0x00471F20, anim) to update them properly
void UpdateSkeletonNodes(bool toggleNodes) {
	NiNode * node;
	HighProcess * proc = static_cast<HighProcess *>((*g_thePlayer)->process);
	UInt8 isCombatMode = proc->unk114;

	ActorAnimData * animData = proc->animData;

	if (g_bHideShieldWeaponSheathed && proc->GetEquippedAmmoData(1)) { // GetEquippedShieldData()
		if (isCombatMode == 0 && proc->IsBlocking() == 0 && IsEquippingWeapon() == 0) {
			NiNode * nLForearmTwist = animData->niNodes24[1];
			if (nLForearmTwist && nLForearmTwist->m_children.numObjs > 0) {
				NiAVObject * nShieldObject = (NiAVObject *)nLForearmTwist->m_children.data[0];
				if (nShieldObject) {
					if (toggleNodes) {
						nShieldObject->m_fLocalScale = 1;
					} else {
						nShieldObject->m_fLocalScale = 0;
					}
				}
			}
		}
	}

	// Fix torch bug wheh shadows are enabled
	if ((*g_thePlayer)->horseOrRider && g_bHorseFirstPersonBody != 1) {
		if (proc->GetEquippedWeaponData(1)) { // GetEquippedTorchData()
			NiNode * nTorch = animData->niNodes24[2];
			if (nTorch && nTorch->m_children.numObjs > 0) {
				NiAVObject * nTorchObject = (NiAVObject *)nTorch->m_children.data[0];
				if (nTorchObject) {
					if (toggleNodes) {
						nTorchObject->m_fLocalScale = 1;
					} else {
						nTorchObject->m_fLocalScale = 0;
					}
				}
			}
		}
	}

	if (toggleNodes) {
		if ((g_bUseThirdPersonArms == 0 && ((*g_thePlayer)->horseOrRider == 0 || g_bHorseFirstPersonBody != 1)) || IsIdlePlaying(1)) {
			if (g_bVisibleArmsWeaponSheathed == 0 || isCombatMode || IsCastingSpells() || IsEquippingWeapon() || IsIdlePlaying(1)) {
				node = FindNode((*g_thePlayer)->niNode, "Bip01 L Clavicle");
				node->m_fLocalScale = 1;
				node = FindNode((*g_thePlayer)->niNode, "Bip01 R Clavicle");
				node->m_fLocalScale = 1;
			} else if (proc->IsBlocking() || proc->GetEquippedWeaponData(1)) { // GetEquippedTorchData()
				node = FindNode((*g_thePlayer)->niNode, "Bip01 L Clavicle");
				node->m_fLocalScale = 1;
			}
		}
		if (g_bEnableHeadFirstPerson == 0 || InDialogMenu()) {
			node = animData->niNodes24[4]; // Bip01 Head
			node->m_fLocalScale = 1;
		}
	} else {
		if ((g_bUseThirdPersonArms == 0 && ((*g_thePlayer)->horseOrRider == 0 || g_bHorseFirstPersonBody != 1)) || IsIdlePlaying(1)) {
			if (g_bVisibleArmsWeaponSheathed == 0 || isCombatMode || IsCastingSpells() || IsEquippingWeapon() || IsIdlePlaying(1)) {
				node = FindNode((*g_thePlayer)->niNode, "Bip01 L Clavicle");
				node->m_fLocalScale = 0;
				node = FindNode((*g_thePlayer)->niNode, "Bip01 R Clavicle");
				node->m_fLocalScale = 0;
			} else if (proc->IsBlocking() || proc->GetEquippedWeaponData(1)) { // GetEquippedTorchData()
				node = FindNode((*g_thePlayer)->niNode, "Bip01 L Clavicle");
				node->m_fLocalScale = 0;
			}
		}
		if (g_bEnableHeadFirstPerson == 0 || InDialogMenu()) {
			node = animData->niNodes24[4]; // Bip01 Head
			node->m_fLocalScale = 0;
		}
	}
}



void UpdateSwitchPOV() {

	g_isThirdPerson = (*g_thePlayer)->isThirdPerson;

	NiNode * nRoot1st = (*g_thePlayer)->firstPersonNiNode;
	NiNode * nRoot3rd = (*g_thePlayer)->niNode;

	HighProcess * proc = static_cast<HighProcess *>((*g_thePlayer)->process);
	int isVanityMode = *(UInt8 *)0x00B3BB04;

	// Reset this here, just in case
	g_isForceThirdPerson = 0;
	g_checkIdle = 0;

	if (g_saveThirdPersonZoom) {
		if ((*g_thePlayer)->isThirdPerson == 0 && isVanityMode == 0) {
			float * thirdPersonZoom = (float *)0x00B3BB24;
			*thirdPersonZoom = g_saveThirdPersonZoom;
			g_saveThirdPersonZoom = 0;
		}
	}

	if (g_bEnableFirstPersonBody) {
		if ((*g_thePlayer)->isThirdPerson == 0 && isVanityMode == 0) {

			if (g_inDialogueMenu || ((*g_thePlayer)->horseOrRider && g_bHorseFirstPersonBody == 0)) {
				nRoot3rd->m_flags |= 0x0001;
			} else {
				nRoot3rd->m_flags &= 0xFFFE;
			}

			// Note: Visible first person body while riding a horse results in visual glitches
			if ((g_bUseThirdPersonArms && (*g_thePlayer)->horseOrRider == 0) || ((*g_thePlayer)->horseOrRider && g_bHorseFirstPersonBody == 1)) {
				nRoot1st->m_flags |= 0x0001;
			} else {
				nRoot1st->m_flags &= 0xFFFE;
			}

			if ((*g_thePlayer)->horseOrRider) {
				g_horseToggle = 1;
			}

		} else {
			g_horseToggle = 0;

			nRoot3rd->m_flags &= 0xFFFE;
			nRoot1st->m_flags |= 0x0001;

			if (g_bFirstPersonModAnim) {
				g_isForceThirdPerson = 2;
				g_checkIdle = 1;
			}
		}
	}
}

const UInt32 kSwitchPOVHook = 0x006650C9;
const UInt32 kSwitchPOVReturn = 0x006650CE;
const UInt32 kSwitchPOVCall = 0x005E5480;

// Hooks the code to switch POV
static _declspec(naked) void HookSwitchPOV(void) {
	_asm
	{
		//overwritten code
		call kSwitchPOVCall

		pushad
		call UpdateSwitchPOV
		popad

		jmp kSwitchPOVReturn
	}
}


// Used to rotate a node in the root node's bonespace
void RotateNode(Actor * act, NiNode * node, float rad) {

	NiMatrix33 mRootInverse;
	NiMatrix33 mNodeWorld;
	NiMatrix33 mParentWorld;
	NiMatrix33 mParentInverse;
	NiMatrix33 mRotate1;
	NiMatrix33 mRotate2;

	// Call UpdateTransform() to update the node's world rotation matrix.
	// It seems to not be modified in the animations, so not updating it causes a bug.
	node->UpdateTransform();

	// Calculate the inverse of the root node
	NiNode * nRoot;
	nRoot = act->niNode;

	MatrixInverse(&mRootInverse, &(nRoot->m_worldRotate));


	// Calculates the NodeWorldMatrix and ParentWorldMatrix without the root node
	NiNode * nParent = static_cast<NiNode *>(node->m_parent);
	MatrixMultiply(&mNodeWorld, &mRootInverse, &(node->m_worldRotate));
	MatrixMultiply(&mParentWorld, &mRootInverse, &(nParent->m_worldRotate));


	// Calculates the rotation matrix in Z-axis
	mRotate1.data[0] = 1.0f;
	mRotate1.data[1] = 0.0f;
	mRotate1.data[2] = 0.0f;
	mRotate1.data[3] = 0.0f;
	mRotate1.data[4] = cos(rad);
	mRotate1.data[5] = -sin(rad);
	mRotate1.data[6] = 0.0f;
	mRotate1.data[7] = sin(rad);
	mRotate1.data[8] = cos(rad);


	// Applies rotation to NodeWorldMatrix
	MatrixMultiply(&mRotate2, &mRotate1, &mNodeWorld);

	// Applies the ParentWorldMatrix inverse to get the node local matrix
	MatrixInverse(&mParentInverse, &mParentWorld);
	MatrixMultiply(&(node->m_localRotate), &mParentInverse, &mRotate2);
}


// Rotate the arms when the player looks up and down
void RotateArms() {

	HighProcess * proc = static_cast<HighProcess *>((*g_thePlayer)->process);
	ActorAnimData * animData = proc->animData;

	float playerAngle = (*g_thePlayer)->rotX;
	float rotateLClavicle = 0.0f;
	float rotateRClavicle = 0.0f;


	// Determines the type of the equipped weapon
	int weaponType = -1;
	ExtraContainerChanges::EntryData * weaponData = proc->equippedWeaponData;
	if (weaponData) {
		TESForm * form = weaponData->type;
		if (form && form->typeID == kFormType_Weapon) {
			TESObjectWEAP * weaponForm = static_cast<TESObjectWEAP *>(form);
			weaponType = weaponForm->type;
		}
	}


	if (animData && animData->FindAnimInRange(TESAnimGroup::kAnimGroup_CastSelf, TESAnimGroup::kAnimGroup_CastTargetAlt)) {
		if (animData->FindAnimInRange(TESAnimGroup::kAnimGroup_CastTouch, TESAnimGroup::kAnimGroup_CastTarget) || animData->FindAnimInRange(TESAnimGroup::kAnimGroup_CastTouchAlt, TESAnimGroup::kAnimGroup_CastTargetAlt)) {
			rotateLClavicle = playerAngle;
			rotateRClavicle = playerAngle;
		}
	} else if (proc->IsAttacking()) {
		if (weaponType == TESObjectWEAP::kType_BladeOneHand || weaponType == TESObjectWEAP::kType_BluntOneHand) {
			rotateRClavicle = playerAngle * 0.35;
		} else if (weaponType == TESObjectWEAP::kType_Bow) {
			rotateLClavicle = playerAngle * 0.35;
			rotateRClavicle = playerAngle * 0.35 * 0.5;
		} else {
			rotateLClavicle = playerAngle * 0.35;
			rotateRClavicle = playerAngle * 0.35;
		}
	} else if (proc->IsBlocking()) {
		if (weaponType == TESObjectWEAP::kType_BladeOneHand || weaponType == TESObjectWEAP::kType_BluntOneHand) {
			if (proc->GetEquippedAmmoData(1)) { // GetEquippedShieldData()
				rotateLClavicle = playerAngle * 0.4;
			} else {
				rotateRClavicle = playerAngle * 0.4;
			}
		} else if (weaponType == TESObjectWEAP::kType_BladeTwoHand || weaponType == TESObjectWEAP::kType_BluntTwoHand) {
			rotateLClavicle = playerAngle * 0.4;
			rotateRClavicle = playerAngle * 0.4;
		} else if (weaponType == TESObjectWEAP::kType_Bow) {
			rotateLClavicle = playerAngle * 0.4;
		} else if (weaponType == TESObjectWEAP::kType_Staff) {
			rotateLClavicle = playerAngle * 0.4;
			rotateRClavicle = playerAngle * 0.4;
		} else { // HandToHand
			rotateLClavicle = playerAngle * 0.4;
			rotateRClavicle = playerAngle * 0.4;
		}
	} else if (proc->GetEquippedWeaponData(1)) { // GetEquippedTorchData()
		rotateLClavicle = playerAngle * 0.6;
	}

	// Fix magicNode position for casting animations
	if (animData && (animData->FindAnimInRange(TESAnimGroup::kAnimGroup_CastTouch, TESAnimGroup::kAnimGroup_CastTarget) || animData->FindAnimInRange(TESAnimGroup::kAnimGroup_CastTouchAlt, TESAnimGroup::kAnimGroup_CastTargetAlt))) {
		NiNode * nMagicNode;
		NiNode * nParentNode;
		NiNode * node;
		NiVector3 v, v1, v2;

		nMagicNode = FindNode((*g_thePlayer)->niNode, "magicNode");
		nParentNode = static_cast<NiNode *>(nMagicNode->m_parent);

		if (animData->FindAnimInRange(TESAnimGroup::kAnimGroup_CastTouch)) {
			NiNode * n1, * n2;
			NiVector3 w1, w2;
			n1 = FindNode((*g_thePlayer)->niNode, "Bip01 L Hand");
			n2 = FindNode((*g_thePlayer)->niNode, "Bip01 R Hand");

			v.x = 12.0;
			v.y = 4.0;
			v.z = 0.0;
			MatrixVectorMultiply(&w1, &(n1->m_worldRotate), &v);
			MatrixVectorMultiply(&w2, &(n2->m_worldRotate), &v);

			v.x = 0.5 * ((n1->m_worldTranslate).x + w1.x + (n2->m_worldTranslate).x + w2.x);
			v.y = 0.5 * ((n1->m_worldTranslate).y + w1.y + (n2->m_worldTranslate).y + w2.y);
			v.z = 0.5 * ((n1->m_worldTranslate).z + w1.z + (n2->m_worldTranslate).z + w2.z);

		} else if (animData->FindAnimInRange(TESAnimGroup::kAnimGroup_CastTouchAlt)) {
			node = FindNode((*g_thePlayer)->niNode, "Bip01 R Hand");
			v1.x = 6.58;
			v1.y = 3.05;
			v1.z = -0.1;
			MatrixVectorMultiply(&v2, &(node->m_worldRotate), &v1);

			v.x = (node->m_worldTranslate).x + v2.x;
			v.y = (node->m_worldTranslate).y + v2.y;
			v.z = (node->m_worldTranslate).z + v2.z;

		} else if (animData->FindAnimInRange(TESAnimGroup::kAnimGroup_CastTarget)) {
			node = FindNode((*g_thePlayer)->niNode, "Bip01 L Hand");
			v1.x = 12.2;
			v1.y = 4.1;
			v1.z = 0.0;
			MatrixVectorMultiply(&v2, &(node->m_worldRotate), &v1);

			v.x = (node->m_worldTranslate).x + v2.x;
			v.y = (node->m_worldTranslate).y + v2.y;
			v.z = (node->m_worldTranslate).z + v2.z;

		} else if (animData->FindAnimInRange(TESAnimGroup::kAnimGroup_CastTargetAlt)) {
			node = FindNode((*g_thePlayer)->niNode, "Bip01 R Hand");
			v1.x = 6.62;
			v1.y = 3.02;
			v1.z = -0.075;
			MatrixVectorMultiply(&v2, &(node->m_worldRotate), &v1);

			v.x = (node->m_worldTranslate).x + v2.x;
			v.y = (node->m_worldTranslate).y + v2.y;
			v.z = (node->m_worldTranslate).z + v2.z;

		}

		NiVector3 wTranslate;
		wTranslate.x = v.x - (nParentNode->m_worldTranslate).x;
		wTranslate.y = v.y - (nParentNode->m_worldTranslate).y;
		wTranslate.z = v.z - (nParentNode->m_worldTranslate).z;

		NiMatrix33 mParentInverse;
		MatrixInverse(&mParentInverse, &(nParentNode->m_worldRotate));
		MatrixVectorMultiply(&(nMagicNode->m_localTranslate), &mParentInverse, &wTranslate);
	}

	NiNode * node;
	node = FindNode((*g_thePlayer)->niNode, "Bip01 L Clavicle");
	RotateNode((*g_thePlayer), node, -rotateLClavicle);

	node = FindNode((*g_thePlayer)->niNode, "Bip01 R Clavicle");
	RotateNode((*g_thePlayer), node, -rotateRClavicle);
}



// This code translates the first person skeleton to the correct position
// and allows you to see the hands on the 1st person body while seeing legs from 3rd person body
void TranslateFirstPerson() {

	// Calculate displacement of first person camera
	NiNode * nRoot1st = (*g_thePlayer)->firstPersonNiNode;
	NiNode * nCamera1st = *g_FirstPersonCameraNode;
	NiVector3 vFirstPerson;
	NiVector3 vThirdPerson;

	vFirstPerson.x = nCamera1st->m_worldTranslate.x - nRoot1st->m_worldTranslate.x;
	vFirstPerson.y = nCamera1st->m_worldTranslate.y - nRoot1st->m_worldTranslate.y;
	vFirstPerson.z = nCamera1st->m_worldTranslate.z - nRoot1st->m_worldTranslate.z;


	// Calculate displacement of third person camera
	NiNode * nRoot3rd = (*g_thePlayer)->niNode;
	NiNode * nHead = FindNode((*g_thePlayer)->niNode, "Bip01 Head");
	NiVector3 * tHead = &(nHead->m_worldTranslate);
	NiVector3 v0, v1;

	v0.x = g_fCameraPosX * nRoot3rd->m_worldScale;
	v0.y = g_fCameraPosY * nRoot3rd->m_worldScale;
	v0.z = g_fCameraPosZ * nRoot3rd->m_worldScale;
	MatrixVectorMultiply(&v1, &(nRoot3rd->m_worldRotate), &v0);


	vThirdPerson.x = (tHead->x + v1.x) - nRoot3rd->m_worldTranslate.x;
	vThirdPerson.y = (tHead->y + v1.y) - nRoot3rd->m_worldTranslate.y;
	vThirdPerson.z = (tHead->z + v1.z) - nRoot3rd->m_worldTranslate.z;


	// This should give the difference between the root node positions
	NiVector3 vTranslateRoot;
	vTranslateRoot.x = nRoot3rd->m_localTranslate.x - nRoot1st->m_localTranslate.x;
	vTranslateRoot.y = nRoot3rd->m_localTranslate.y - nRoot1st->m_localTranslate.y;
	vTranslateRoot.z = nRoot3rd->m_localTranslate.z - nRoot1st->m_localTranslate.z;


	// Translate 1st person root node to new position
	nRoot1st->m_localTranslate.x += vThirdPerson.x - vFirstPerson.x + vTranslateRoot.x;
	nRoot1st->m_localTranslate.y += vThirdPerson.y - vFirstPerson.y + vTranslateRoot.y;
	nRoot1st->m_localTranslate.z += vThirdPerson.z - vFirstPerson.z + vTranslateRoot.z;

}


// Translates the third person skeleton to correct position
void TranslateThirdPerson() {

	NiNode * nRoot3rd = (*g_thePlayer)->niNode;

	HighProcess * proc = static_cast<HighProcess *>((*g_thePlayer)->process);
	int sitSleepFlag = proc->unk11D;

	// Move body to prevent seeing into objects
	if (sitSleepFlag == 0) {
		if (g_bEnableHeadBob == 0) {
			NiNode * nCamera1st = *g_FirstPersonCameraNode;

			ActorAnimData * animData = proc->animData;
			NiNode * nHead3rd = animData->niNodes24[4];

			NiVector3 v0, v1;
			v0.x = g_fCameraPosX * nRoot3rd->m_worldScale;
			v0.y = g_fCameraPosY * nRoot3rd->m_worldScale;
			v0.z = g_fCameraPosZ * nRoot3rd->m_worldScale;
			MatrixVectorMultiply(&v1, &(nRoot3rd->m_worldRotate), &v0);

			nRoot3rd->m_localTranslate.x += nCamera1st->m_worldTranslate.x - (nHead3rd->m_worldTranslate.x + v1.x);
			nRoot3rd->m_localTranslate.y += nCamera1st->m_worldTranslate.y - (nHead3rd->m_worldTranslate.y + v1.y);
			nRoot3rd->m_localTranslate.z += nCamera1st->m_worldTranslate.z - (nHead3rd->m_worldTranslate.z + v1.z);

		} else if (g_bEnableHeadBob == 2 || IsSwimming(*g_thePlayer)) {
			ActorAnimData * animData = proc->animData;
			NiNode * nHead = animData->niNodes24[4];

			NiVector3 v0, v1;
			v0.x = g_fCameraPosX * nRoot3rd->m_worldScale;
			v0.y = g_fCameraPosY * nRoot3rd->m_worldScale;
			v0.z = g_fCameraPosZ * nRoot3rd->m_worldScale;
			MatrixVectorMultiply(&v1, &(nRoot3rd->m_worldRotate), &v0);

			nRoot3rd->m_localTranslate.x += (nRoot3rd->m_worldTranslate.x - (nHead->m_worldTranslate.x + v1.x)) * 0.80;
			nRoot3rd->m_localTranslate.y += (nRoot3rd->m_worldTranslate.y - (nHead->m_worldTranslate.y + v1.y)) * 0.80;

		} else {
			NiVector3 v0, v1;
			if (IsSneaking(*g_thePlayer)) {
				v0.x = 0.0 * nRoot3rd->m_worldScale;
				v0.y = -35.0 * nRoot3rd->m_worldScale;
				v0.z = 0.0 * nRoot3rd->m_worldScale;
			} else {
				v0.x = 0.0 * nRoot3rd->m_worldScale;
				v0.y = -25.0 * nRoot3rd->m_worldScale;
				v0.z = 0.0 * nRoot3rd->m_worldScale;
			}
			MatrixVectorMultiply(&v1, &(nRoot3rd->m_worldRotate), &v0);

			nRoot3rd->m_localTranslate.x += v1.x;
			nRoot3rd->m_localTranslate.y += v1.y;
			nRoot3rd->m_localTranslate.z += v1.z;
		}
	}
}


// Repositions the skeleton to match the camera
// Note: This function is actually called twice by the game for both 1st/3rd person skeletons
//       (*g_thePlayer)->isThirdPerson gets set to corresponding values
void UpdateActor(Actor * act, ActorAnimData * actAnimData) {

	if (act == (*g_thePlayer) && (g_isThirdPerson == 0 || g_isForceThirdPerson)) {

		// Skip updating particles here to fix floating torch flame bug
		g_animSkipParticleUpdate = 1;
		ThisStdCall(0x00471F20, actAnimData); // applies animData to skeleton

		HighProcess * proc = static_cast<HighProcess *>((*g_thePlayer)->process);
		int knockedFlag = proc->unk11C;

		// Translate camera up to avoid clipping
		// with the ground when dead or knocked out
		if (g_isForceThirdPerson && ((*g_thePlayer)->IsDead() || knockedFlag != 0)) {
			g_animSkipParticleUpdate = 0;

			NiNode * nRoot3rd = (*g_thePlayer)->niNode;
			nRoot3rd->m_localTranslate.z += g_fCameraZOffset * nRoot3rd->m_worldScale;

			g_animSkipCollisions = 1;
			ThisStdCall(0x00471F20, proc->animData); // applies animData to skeleton
			g_animSkipCollisions = 0;

		// Disabled in dialog to avoid zoom glitch
		} else if (InDialogMenu() == 0) {

			NiNode * nRoot3rd = (*g_thePlayer)->niNode;
			NiNode * nRoot1st = (*g_thePlayer)->firstPersonNiNode;

			int sitSleepFlag = proc->unk11D;
			int isVanityMode = *(UInt8 *)0x00B3BB04;

			// Detect 1st person animation data
			if ((*g_thePlayer)->isThirdPerson == 0 || ((*g_thePlayer)->isThirdPerson && g_isForceThirdPerson)) {

				// Enables body if the horse dies while riding in 1st person
				if (g_bEnableFirstPersonBody) {
					if (g_horseToggle && (*g_thePlayer)->horseOrRider == 0) {
						nRoot3rd->m_flags &= 0xFFFE;

						if (g_bUseThirdPersonArms) {
							nRoot1st->m_flags |= 0x0001;
						} else {
							nRoot1st->m_flags &= 0xFFFE;
						}

						g_horseToggle = 0;
					}
				}

				// Moves the camera when sleeping/vampire feeding to avoid clipping
				if (sitSleepFlag >= 8 && isVanityMode == 0) {
					if (g_isThirdPerson == 0 || g_isForceThirdPerson) {
						if (IsVampireFeeding(*g_thePlayer)) {
							NiVector3 v0, v1;
							v0.x = 0.0 * nRoot3rd->m_worldScale;
							v0.y = g_fVampireFeedXYOffset * nRoot3rd->m_worldScale;
							v0.z = 0.0 * nRoot3rd->m_worldScale;
							MatrixVectorMultiply(&v1, &(nRoot3rd->m_worldRotate), &v0);

							nRoot3rd->m_localTranslate.x += v1.x;
							nRoot3rd->m_localTranslate.y += v1.y;
							nRoot3rd->m_localTranslate.z += v1.z + g_fVampireFeedZOffset * nRoot3rd->m_worldScale;
						} else {
							nRoot3rd->m_localTranslate.z += g_fCameraZOffset * nRoot3rd->m_worldScale;
						}
					}
				}

				if (g_isThirdPerson == 0 && isVanityMode == 0 && g_isForceThirdPerson == 0 && ((*g_thePlayer)->horseOrRider == 0 || g_bHorseFirstPersonBody)) {

					if (g_bEnableHeadBob || g_bEnableFirstPersonBody) {
						TranslateThirdPerson();

						// Rotate Arms
						if (g_bRotateThirdPersonArms & 1) {
							RotateArms();
						}
					}

					if (g_bEnableHeadBob || sitSleepFlag != 0) {
						TranslateFirstPerson();

						// Hide 1st person arms when sleeping
						if (sitSleepFlag >= 8) {
							nRoot1st->m_localTranslate.z -= 100;
						}

						ThisStdCall(0x00471F20, (*g_thePlayer)->firstPersonAnimData); // applies animData to skeleton
					}

				} else {
					// Rotate Arms
					if (g_bRotateThirdPersonArms & 2) {
						RotateArms();
					}
				}

				ThisStdCall(0x00471F20, proc->animData); // applies animData to skeleton

			} else if ((*g_thePlayer)->isThirdPerson && g_isThirdPerson && g_bRotateThirdPersonArms & 2) {
				RotateArms();
				ThisStdCall(0x00471F20, proc->animData); // applies animData to skeleton
			}
		}

		g_animSkipParticleUpdate = 0;

	} else {
		ThisStdCall(0x00471F20, actAnimData); // applies animData to skeleton
	}
}


// Address of function called by overwritten code
const UInt32 kApplyAnimDataCall = 0x00471F20;

const UInt32 kAnimationHook = 0x006043DC;
const UInt32 kAnimationReturn = 0x006043E1;

// Hooks Oblivion's animation code in order to edit the animations
static _declspec(naked) void HookAnimation(void)
{
	_asm
	{
		pushad
		push ecx
		push ebp
		call UpdateActor
		add esp, 8
		popad

		jmp kAnimationReturn
	}
}


// Modifies camera position
void UpdateCamera(NiNode * nCamera) {

	HighProcess * proc = static_cast<HighProcess *>((*g_thePlayer)->process);
	static int sitSleepFlag = 0;
	int knockedFlag = proc->unk11C;
	int isVanityMode = *(UInt8 *)0x00B3BB04;

	g_isThirdPerson = (*g_thePlayer)->isThirdPerson;

	// Apply animation data to update particles
	// Causes bug if the player is dead
	if (g_isThirdPerson == 0 || g_isForceThirdPerson) {
		if ((*g_thePlayer)->IsDead() == 0) {
			g_animSkipCollisions = 1;
			ThisStdCall(0x00471F20, proc->animData);
			ThisStdCall(0x00471F20, (*g_thePlayer)->firstPersonAnimData);
			g_animSkipCollisions = 0;
		}
	}

	if (g_bEnableFirstPersonBody) {
		// Fixes a bug with the view briefly flickering to 3rd person when sitting
		if (g_isThirdPerson != (*g_thePlayer)->isThirdPerson) {
			if (g_isThirdPerson == 0) {
				g_isForceThirdPerson = 1;
				sitSleepFlag = 5;
			}
		}
	}

	// Detects if player is entering dialogue menu
	InterfaceManager * im = InterfaceManager::GetSingleton();
	if (g_inDialogueMenu == 0) {
		if (!im->IsGameMode() && (im->MenuModeHasFocus(kMenuType_Dialog) || im->MenuModeHasFocus(kMenuType_Persuasion))) {
			g_inDialogueMenu = 1;
		}
	}

	// Detects if a default loiter idle is playing and disables it
	if (!(*g_thePlayer)->isThirdPerson && IsIdlePlaying()) {
		ActorAnimData * animData = proc->animData;
		if (animData->PathsInclude("Stand1.kf") || animData->PathsInclude("IdleGuardCheckScabbard.kf")) {
			(*g_thePlayer)->isThirdPerson = 1;
			ThisStdCall(0x00601B80, (*g_thePlayer)); // calls evp on the player
			(*g_thePlayer)->isThirdPerson = g_isThirdPerson;
		}
	}

	// Enables idle animations to play in fake 1st person
	if (g_bFirstPersonModAnim == 1) {
		(*g_thePlayer)->isThirdPerson = 1;
		if (g_isForceThirdPerson == 2) {
			if (IsIdlePlaying() && g_idlePlaying != 2) {
				g_idlePlaying = 3;
				g_isForceThirdPerson = 1;
				g_checkIdle = 0;
			} else {
				g_checkIdle--;
				if (g_checkIdle == 0) {
					g_isForceThirdPerson = 0;
				}
			}
		}
		if (IsIdlePlaying()) {
			if (g_idlePlaying < 2) {
				g_idlePlaying++;
			}
		} else {
			g_idlePlaying = 0;
		}
		(*g_thePlayer)->isThirdPerson = g_isThirdPerson;

	} else {
		if (g_isForceThirdPerson == 2) {
			if (IsIdlePlaying()) {
				g_isForceThirdPerson = 1;
				g_checkIdle = 0;
			} else {
				g_checkIdle--;
				if (g_checkIdle == 0) {
					g_isForceThirdPerson = 0;
				}
			}
		}
	}

	// Do not show body if dead or knocked out
	// (Camera placed inside head to avoid ground clipping)
	NiNode * nRoot3rd = (*g_thePlayer)->niNode;
	NiNode * nRoot1st = (*g_thePlayer)->firstPersonNiNode;

	if (g_inDialogueMenu || ((*g_thePlayer)->isThirdPerson == 0 && isVanityMode == 0 && (*g_thePlayer)->horseOrRider && g_bHorseFirstPersonBody == 0)) {
		nRoot3rd->m_flags |= 0x0001;
	} else if (g_bEnableFirstPersonBody) {
		nRoot3rd->m_flags &= 0xFFFE;
	}

	if (g_bEnableFirstPersonBody) {
		if (g_bUseThirdPersonArms) {
			if ((*g_thePlayer)->isThirdPerson == 0 && isVanityMode == 0 && (IsIdlePlaying(1) || ((*g_thePlayer)->horseOrRider && g_bHorseFirstPersonBody != 1))) {
				nRoot1st->m_flags &= 0xFFFE;
			} else {
				nRoot1st->m_flags |= 0x0001;
			}
		} else if (g_bVisibleArmsWeaponSheathed == 1) {
			UInt8 isCombatMode = proc->unk114;
			bool torchEquipped = proc->GetEquippedWeaponData(1); // GetEquippedTorchData()
			bool weaponSheathed = (isCombatMode == 0 && IsCastingSpells() == 0 && IsEquippingWeapon() == 0 && proc->IsBlocking() == 0 && torchEquipped == 0);
			if ((*g_thePlayer)->isThirdPerson == 0 && isVanityMode == 0 && (((*g_thePlayer)->horseOrRider == 0 && weaponSheathed == 0) || IsIdlePlaying(1) || ((*g_thePlayer)->horseOrRider && g_bHorseFirstPersonBody != 1))) {
				nRoot1st->m_flags &= 0xFFFE;
			} else {
				nRoot1st->m_flags |= 0x0001;
			}
		}
	}


	// Set sittingFlag to detect if player is transitioning
	// into sit state (3), out of sit state (5), or is in a bed state (>=8)
	// unk11D is SitSleepState
	if (proc->unk11D == 3 || proc->unk11D == 5 || proc->unk11D >= 8) {
		sitSleepFlag = 5;
	// Must wait a few frames after the GetSitting state changes
	// or the camera does not update properly
	} else if (sitSleepFlag > 0) {
		sitSleepFlag -= 1;
	}



	UInt8 testAnim1 = *((UInt8 *)(*g_thePlayer) + 0x71C); // Sit Down, Mount Horse, Vampire Feed
	UInt8 testAnim2 = *((UInt8 *)(*g_thePlayer) + 0x71D); // Sit GetUp, Dismount Horse
	if (g_saveThirdPersonZoom != 0 && knockedFlag == 0 && testAnim1 == 0 && testAnim2 == 0) {
		float * thirdPersonZoom = (float *)0x00B3BB24;
		*thirdPersonZoom = g_saveThirdPersonZoom;
		g_saveThirdPersonZoom = 0;
	}

	// Disabled when in 3rd person
	if ((*g_thePlayer)->isThirdPerson && g_isForceThirdPerson == 0) {
		return;

	// Disabled when riding a horse
	} else if ((*g_thePlayer)->horseOrRider && g_bHorseFirstPersonBody == 0 && proc->unk11D == 4 && sitSleepFlag == 0) {
		return;

	// Disabled in dialog to avoid zoom glitch
	} else if (InDialogMenu() || isVanityMode) {
		return;
	}


	NiNode * nHead = FindNode((*g_thePlayer)->niNode, "Bip01 Head");
	NiVector3 * tHead = &(nHead->m_worldTranslate);
	NiVector3 * tCamera = &(nCamera->m_localTranslate);
	NiVector3 v0, v1;


	// Positions the camera in front of the head
	if ((*g_thePlayer)->IsDead() || knockedFlag != 0 || sitSleepFlag != 0) {
		if (IsVampireFeeding(*g_thePlayer) || (*g_thePlayer)->horseOrRider) {
			// Do not move as far to prevent clipping
			v0.x = g_fCameraPosZ * nRoot3rd->m_worldScale;
			if (IsVampireFeeding(*g_thePlayer)) {
				v0.y = g_fVampireFeedCameraPosY * nRoot3rd->m_worldScale;
			} else {
				v0.y = g_fHorseMountCameraPosY * nRoot3rd->m_worldScale;
			}
			v0.z = g_fCameraPosX * nRoot3rd->m_worldScale;
		} else {
			v0.x = g_fCameraPosZ * nRoot3rd->m_worldScale;
			v0.y = g_fCameraPosY * nRoot3rd->m_worldScale;
			v0.z = g_fCameraPosX * nRoot3rd->m_worldScale;
		}
		MatrixVectorMultiply(&v1, &(nHead->m_worldRotate), &v0);

		tCamera->x = tHead->x + v1.x;
		tCamera->y = tHead->y + v1.y;
		tCamera->z = tHead->z + v1.z;

		// Rotate camera
		if (g_bFirstPersonAnimCameraRotation && (*g_thePlayer)->IsDead() == 0 && IsVampireFeeding(*g_thePlayer) == 0) {
			NiMatrix33 m;
			NiVector3 v;
			v.x = -90.0*PI/180;
			v.y = 90.0*PI/180 - (*g_thePlayer)->rotX;
			v.z = 90.0*PI/180;
			//EulerToMatrix(&mCameraRot, &v);

			m.data[0] = cos(v.y)*cos(v.z);
			m.data[1] = -cos(v.y)*sin(v.z);
			m.data[2] = sin(v.y);
			m.data[3] = cos(v.z)*sin(v.x)*sin(v.y) + cos(v.x)*sin(v.z);
			m.data[4] = cos(v.x)*cos(v.z) - sin(v.x)*sin(v.y)*sin(v.z);
			m.data[5] = -cos(v.y)*sin(v.x);
			m.data[6] = -cos(v.x)*cos(v.z)*sin(v.y) + sin(v.x)*sin(v.z);
			m.data[7] = cos(v.z)*sin(v.x) + cos(v.x)*sin(v.y)*sin(v.z);
			m.data[8] = cos(v.x)*cos(v.y);

			MatrixMultiply(&(nCamera->m_localRotate), &(nHead->m_worldRotate), &m);

		} else {
			NiMatrix33 mCameraRot;
			mCameraRot.data[0] = 0;
			mCameraRot.data[1] = 0;
			mCameraRot.data[2] = 1;
			mCameraRot.data[3] = 0;
			mCameraRot.data[4] = 1;
			mCameraRot.data[5] = 0;
			mCameraRot.data[6] = -1;
			mCameraRot.data[7] = 0;
			mCameraRot.data[8] = 0;

			MatrixMultiply(&(nCamera->m_localRotate), &(nHead->m_worldRotate), &mCameraRot);
		}

	} else if (IsIdlePlaying()) {

		v0.x = g_fCameraPosZ * nRoot3rd->m_worldScale;
		v0.y = g_fCameraPosY * nRoot3rd->m_worldScale;
		v0.z = g_fCameraPosX * nRoot3rd->m_worldScale;
		MatrixVectorMultiply(&v1, &(nHead->m_worldRotate), &v0);

		tCamera->x = tHead->x + v1.x;
		tCamera->y = tHead->y + v1.y;
		tCamera->z = tHead->z + v1.z;


		NiMatrix33 m;

		NiVector3 v;
		v.x = -90.0*PI/180;
		v.y = 90.0*PI/180 - (*g_thePlayer)->rotX;
		v.z = 90.0*PI/180;
		//EulerToMatrix(&mCameraRot, &v);

		m.data[0] = cos(v.y)*cos(v.z);
		m.data[1] = -cos(v.y)*sin(v.z);
		m.data[2] = sin(v.y);
		m.data[3] = cos(v.z)*sin(v.x)*sin(v.y) + cos(v.x)*sin(v.z);
		m.data[4] = cos(v.x)*cos(v.z) - sin(v.x)*sin(v.y)*sin(v.z);
		m.data[5] = -cos(v.y)*sin(v.x);
		m.data[6] = -cos(v.x)*cos(v.z)*sin(v.y) + sin(v.x)*sin(v.z);
		m.data[7] = cos(v.z)*sin(v.x) + cos(v.x)*sin(v.y)*sin(v.z);
		m.data[8] = cos(v.x)*cos(v.y);

		MatrixMultiply(&(nCamera->m_localRotate), &(nHead->m_worldRotate), &m);

	} else if (g_isForceThirdPerson || g_bEnableHeadBob || (proc->unk11D && g_bEnableFirstPersonBody)) {

		v0.x = g_fCameraPosX * nRoot3rd->m_worldScale;
		v0.y = g_fCameraPosY * nRoot3rd->m_worldScale;
		v0.z = g_fCameraPosZ * nRoot3rd->m_worldScale;
		MatrixVectorMultiply(&v1, &(nRoot3rd->m_worldRotate), &v0);

		tCamera->x = tHead->x + v1.x;
		tCamera->y = tHead->y + v1.y;
		tCamera->z = tHead->z + v1.z;

	}
}


const UInt32 kCameraHook = 0x0066BE6E;
const UInt32 kCameraReturn = 0x0066BE7C;

// Hooks Oblivion's code for updating the camera
static _declspec(naked) void HookCamera(void)
{
	_asm
	{
		push ebx
		push eax
		call UpdateCamera
		add esp, 4
		pop ebx

		jmp kCameraReturn
	}
}


// Checks if head tracking should be disabled on the player
bool CheckDisableHeadtracking(Actor * act) {
	if (act == (*g_thePlayer)) {
		if (g_isThirdPerson == 0 || g_isForceThirdPerson) {
			return true;
		}
	}

	return false;
}


const UInt32 kHeadTrackingHook = 0x00603AAA;
const UInt32 kHeadTrackingReturn01 = 0x00603AB0;
const UInt32 kHeadTrackingReturn02 = 0x00603BB7;

// Hooks Oblivion's head tracking code
static _declspec(naked) void HookHeadTracking(void)
{
	_asm
	{
		// overwritten code
		jz disable_head_ik

		// Check if head ik should be disabled on the player
		push esi
		call CheckDisableHeadtracking
		add esp, 4

		test al, al
		jnz disable_head_ik

		jmp kHeadTrackingReturn01

	disable_head_ik:
		jmp kHeadTrackingReturn02
	}
}


// Fix magic shader not appearing in 1st person
// Shader is applied to both 1st and 3rd person
bool UpdateMagicShader(MagicShaderHitEffect * shader) {
	bool retVal;
	if (shader->target == (*g_thePlayer)) {

		// Restore nodes to avoid bug with paralysis
		if ((*g_thePlayer)->GetActorValue(kActorVal_Paralysis) != 0) {
			UpdateSkeletonNodes(1);

			HighProcess * proc = static_cast<HighProcess *>((*g_thePlayer)->process);
			ThisStdCall(0x00471F20, proc->animData); // applies animData to skeleton
		}

		NiNode * nRoot3rd = (*g_thePlayer)->niNode;
		UInt16 flags = nRoot3rd->m_flags;

		if (g_isThirdPerson || g_bUseThirdPersonArms) {
			nRoot3rd->m_flags |= 0x0001;
			ApplyMagicEffectShader(shader);

			nRoot3rd->m_flags &= 0xFFFE;
			retVal = ApplyMagicEffectShader(shader);
		} else {
			nRoot3rd->m_flags &= 0xFFFE;
			ApplyMagicEffectShader(shader);

			nRoot3rd->m_flags |= 0x0001;
			retVal = ApplyMagicEffectShader(shader);
		}

		nRoot3rd->m_flags = flags;
	} else {
		retVal = ApplyMagicEffectShader(shader);
	}
	return retVal;
}


const UInt32 kFixMagicShader01Hook = 0x006575FA;
const UInt32 kFixMagicShader01Return = 0x00657603;

// Fix body magic shader 1
static _declspec(naked) void HookFixMagicShader01(void)
{
	_asm
	{
		push eax
		call UpdateMagicShader
		add esp, 4

		jmp kFixMagicShader01Return
	}
}


const UInt32 kFixMagicShader02Hook = 0x0069DAEF;
const UInt32 kFixMagicShader02Return = 0x0069DB00;

// Fix body magic shader 2
static _declspec(naked) void HookFixMagicShader02(void)
{
	_asm
	{
		// overwritten code
		mov [esp+0x18], -1

		push esi
		call UpdateMagicShader
		add esp, 4

		jmp kFixMagicShader02Return
	}
}


const UInt32 kFixMagicShader03Hook = 0x0065775B;
const UInt32 kFixMagicShader03Return = 0x00657764;

// Fix weapon magic shader 1
static _declspec(naked) void HookFixMagicShader03(void)
{
	_asm
	{
		push edi
		call UpdateMagicShader
		add esp, 4

		jmp kFixMagicShader03Return
	}
}


const UInt32 kFixMagicShader04Hook = 0x006576DF;
const UInt32 kFixMagicShader04Return = 0x0065777C;

// Fix weapon magic shader 2
static _declspec(naked) void HookFixMagicShader04(void)
{
	_asm
	{
		push eax
		call UpdateMagicShader
		add esp, 4

		jmp kFixMagicShader04Return
	}
}


const UInt32 kFixMagicShader05Hook = 0x005060D2;
const UInt32 kFixMagicShader05Return = 0x005060E3;

// Fix PMS magic shader
static _declspec(naked) void HookFixMagicShader05(void)
{
	_asm
	{
		// overwritten code
		mov [esp+0x20], -1

		push esi
		call UpdateMagicShader
		add esp, 4

		jmp kFixMagicShader05Return
	}
}


// Returns if the camera should be forced if player is sitting or knocked out
void CheckForceCamera() {
	HighProcess * proc = static_cast<HighProcess *>((*g_thePlayer)->process);
	g_isForceThirdPerson = ((proc->unk11C == 0) && g_bFirstPersonSitting) || ((proc->unk11C != 0) && g_bFirstPersonKnockout);
}


const UInt32 kForcePOVCall = 0x0066C580;

const UInt32 kForcePOV01Hook = 0x0066C63D;
const UInt32 kForcePOV01Return = 0x0066C642;

static _declspec(naked) void HookForcePOV01(void)
{
	_asm
	{
		// overwritten code, call Oblivion's switch POV
		call kForcePOVCall

		pushad
		call CheckForceCamera
		popad

		jmp kForcePOV01Return
	}
}


const UInt32 kForcePOV02Hook = 0x0066CEEE;
const UInt32 kForcePOV02Return = 0x0066CEF3;

static _declspec(naked) void HookForcePOV02(void)
{
	_asm
	{
		// overwritten code, call Oblivion's switch POV
		call kForcePOVCall

		mov g_isForceThirdPerson, 1
		jmp kForcePOV02Return
	}
}


const UInt32 kForcePOV03Hook = 0x0066CF5D;
const UInt32 kForcePOV03Return = 0x0066CF62;

static _declspec(naked) void HookForcePOV03(void)
{
	_asm
	{
		// overwritten code, call Oblivion's switch POV
		call kForcePOVCall

		mov g_isForceThirdPerson, 1
		jmp kForcePOV03Return
	}
}

const UInt32 kForcePOV04Hook = 0x0066CCBD;
const UInt32 kForcePOV04Return = 0x0066CCC2;

static _declspec(naked) void HookForcePOV04(void)
{
	_asm
	{
		// overwritten code, call Oblivion's switch POV
		call kForcePOVCall

		mov g_isForceThirdPerson, 1
		jmp kForcePOV04Return
	}
}

const UInt32 kForcePOV05Hook = 0x00600BCC;
const UInt32 kForcePOV05Return = 0x00600BD1;

static _declspec(naked) void HookForcePOV05(void)
{
	_asm
	{
		// overwritten code, call Oblivion's switch POV
		call kForcePOVCall

		mov g_isForceThirdPerson, 1
		jmp kForcePOV05Return
	}
}


void UpdateFixForcePOVZoom() {
	float * thirdPersonZoom = (float *)0x00B3BB24;
	g_saveThirdPersonZoom = *thirdPersonZoom;
}

const UInt32 kFixForcePOVZoomHook = 0x0066C61F;
const UInt32 kFixForcePOVZoomReturn = 0x0066C629;

static _declspec(naked) void HookFixForcePOVZoom(void) {
	_asm
	{
		pushad
		call UpdateFixForcePOVZoom
		popad

		// overwritten code
		fstp DS:[0x0B3BB24]
		jmp kFixForcePOVZoomReturn
	}
}

const UInt32 kFixHorseMountZoomHook = 0x0066CED2;
const UInt32 kFixHorseMountZoomReturn = 0x0066CEDC;

static _declspec(naked) void HookFixHorseMountZoom(void) {
	_asm
	{
		pushad
		call UpdateFixForcePOVZoom
		popad

		// overwritten code
		fstp DS:[0x0B3BB24]
		jmp kFixHorseMountZoomReturn
	}
}

const UInt32 kFixHorseDismountZoomHook = 0x0066CF41;
const UInt32 kFixHorseDismountZoomReturn = 0x0066CF4B;

static _declspec(naked) void HookFixHorseDismountZoom(void) {
	_asm
	{
		pushad
		call UpdateFixForcePOVZoom
		popad

		// overwritten code
		fstp DS:[0x0B3BB24]
		jmp kFixHorseDismountZoomReturn
	}
}

const UInt32 kFixVampireFeedZoomHook = 0x0066CCA0;
const UInt32 kFixVampireFeedZoomReturn = 0x0066CCAA;

static _declspec(naked) void HookFixVampireFeedZoom(void) {
	_asm
	{
		pushad
		call UpdateFixForcePOVZoom
		popad

		// overwritten code
		fstp DS:[0x0B3BB24]
		jmp kFixVampireFeedZoomReturn
	}
}


// Mute 3rd person sounds if in 1st person
int CheckFixSounds(ActorAnimData * animData) {
	HighProcess * proc = static_cast<HighProcess *>((*g_thePlayer)->process);
	ActorAnimData * playerAnimData = proc->animData;
	NiNode * nRoot1st = (*g_thePlayer)->firstPersonNiNode;
	NiNode * nRoot3rd = (*g_thePlayer)->niNode;
	bool bothVisible = (!(nRoot1st->m_flags & 0x0001) && !(nRoot3rd->m_flags & 0x0001));
	return (bothVisible && (animData == playerAnimData));
}


const UInt32 kFixAnimSoundsHook = 0x00477A99;
const UInt32 kFixAnimSoundsReturn01 = 0x00477A9E;
const UInt32 kFixAnimSoundsReturn02 = 0x00477ABA;

// Fix the double sound bug
static _declspec(naked) void HookFixAnimSounds(void) {
	_asm
	{
		pushad
		push esi
		call CheckFixSounds
		add esp, 4

		test eax, eax
		popad
		jnz skip_player_3rd_sound

		// overwritten code
		mov eax, [esp+0x18] // actor
		push eax
		jmp kFixAnimSoundsReturn01

	skip_player_3rd_sound:
		jmp kFixAnimSoundsReturn02
	}
}


const UInt32 kFixPlayerFadeOutHook = 0x0065F4E2;
const UInt32 kFixPlayerFadeOutReturn01 = 0x0065F4E8;
const UInt32 kFixPlayerFadeOutReturn02 = 0x0065F54E;

// Fix bug where player can become invisible in force 3rd person mode
static _declspec(naked) void HookFixPlayerFadeOut(void) {
	_asm
	{
		push eax
		mov eax, g_isForceThirdPerson
		test eax, eax
		pop eax
		jnz skip_transparency

		//overwritten code
		fld DS:[0x00B14E50]

		jmp kFixPlayerFadeOutReturn01

	skip_transparency:
		jmp kFixPlayerFadeOutReturn02
	}
}


const UInt32 kAnimUpdateCollisionHook = 0x0070C159;
const UInt32 kAnimUpdateCollisionReturn01 = 0x0070C15E;
const UInt32 kAnimUpdateCollisionReturn02 = 0x0070C168;

// Allows disabling applying the collision object to animation
static _declspec(naked) void HookAnimUpdateCollision(void) {
	_asm
	{
		//overwritten code
		pop edi
		pop esi
		pop ebx
		je skip_collision

		push eax
		mov eax, g_animSkipCollisions
		test eax, eax
		pop eax
		jne skip_collision

		jmp kAnimUpdateCollisionReturn01

	skip_collision:
		jmp kAnimUpdateCollisionReturn02
	}
}

const UInt32 kAnimUpdateParticleSysHook = 0x007492BE;
const UInt32 kAnimUpdateParticleSysReturn = 0x007492C3;

// Allows disabling particle system update to fix torch flame bug
static _declspec(naked) void HookAnimUpdateParticleSys(void) {
	_asm
	{
		push edx
		mov edx, g_animSkipParticleUpdate
		xor eax, eax
		test edx, edx
		pop edx
		jne skip_update

		// overwritten code
		mov eax, [esp+0x0C]

	skip_update:
		push eax
		jmp kAnimUpdateParticleSysReturn
	}
}


// Updates nodes for shadows
void UpdateShadowNodes(bool toggleNodes) {
	NiNode * nRoot3rd = (*g_thePlayer)->niNode;
	int isVanityMode = *(UInt8 *)0x00B3BB04;

	Actor * horse = (*g_thePlayer)->GetMountedHorse();
	int chameleon = (*g_thePlayer)->GetActorValue(46);
	int invisibility = (*g_thePlayer)->GetActorValue(47);
	static int rootFlags;

	// Disable shadows if invisible and on a horse
	if (horse && (chameleon || invisibility)) {
		if (toggleNodes) {
			rootFlags = nRoot3rd->m_flags;
			nRoot3rd->m_flags |= 0x0001;
		} else {
			nRoot3rd->m_flags = rootFlags;
		}

	} else if ((*g_thePlayer)->isThirdPerson == 0 && isVanityMode == 0) {

		if (toggleNodes) {
			if (g_bEnableFirstPersonBody == 0 || ((*g_thePlayer)->horseOrRider && g_bHorseFirstPersonBody == 0)) {
				nRoot3rd->m_flags &= 0xFFFE;
			}

			HighProcess * proc = static_cast<HighProcess *>((*g_thePlayer)->process);
			ThisStdCall(0x00471F20, proc->animData); // applies animData to skeleton
		} else {
			if (g_bEnableFirstPersonBody == 0 || ((*g_thePlayer)->horseOrRider && g_bHorseFirstPersonBody == 0)) {
				nRoot3rd->m_flags |= 0x0001;
			}

			// Disables visibility of arms/head nodes
			UpdateSkeletonNodes(0);

			HighProcess * proc = static_cast<HighProcess *>((*g_thePlayer)->process);
			ThisStdCall(0x00471F20, proc->animData); // applies animData to skeleton

			// Resets scale to prevent bug with arms/head disappearing
			// when equipping items in the inventory menu
			UpdateSkeletonNodes(1);

			// Fix bug where game uses 3rd person magicNode causing spells to be
			// casted from wrong position. Move it to same position as first person node.
			NiNode * nMagicNodeFirst = FindNode((*g_thePlayer)->firstPersonNiNode, "magicNode");
			NiNode * nMagicNodeThird = FindNode((*g_thePlayer)->niNode, "magicNode");
			nMagicNodeThird->m_worldTranslate.x = nMagicNodeFirst->m_worldTranslate.x;
			nMagicNodeThird->m_worldTranslate.y = nMagicNodeFirst->m_worldTranslate.y;
			nMagicNodeThird->m_worldTranslate.z = nMagicNodeFirst->m_worldTranslate.z;

		}
	}

	if (toggleNodes == 0) {
		// Detects if player is exiting dialogue menu
		InterfaceManager * im = InterfaceManager::GetSingleton();
		if (g_inDialogueMenu != 0) {
			float dialogZoomPercent = *(float *)0x00B13FCC;
			if (im->IsGameMode() && dialogZoomPercent == 0) {
				g_inDialogueMenu = 0;
			}
		}
	}
}


const UInt32 kShadowsHook = 0x0040C91B;
const UInt32 kShadowsReturn = 0x0040C920;
const UInt32 kShadowsCall = 0x004073D0;

// Fix bug with 1st person shadows missing arms/head
static _declspec(naked) void UpdateShadows(void)
{
	_asm
	{
		pushad
		push 1
		call UpdateShadowNodes
		add esp, 4
		popad

		// overwritten code, call Oblivion's shadow function
		call kShadowsCall

		pushad
		push 0
		call UpdateShadowNodes
		add esp, 4
		popad

		jmp kShadowsReturn
	}
}



extern "C" {

bool OBSEPlugin_Query(const OBSEInterface * obse, PluginInfo * info)
{
	//_MESSAGE("query");

	// fill out the info structure
	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = "EnhancedCamera";
	info->version = 1;

	// version checks
	if(!obse->isEditor)
	{
		if(obse->obseVersion < OBSE_VERSION_INTEGER)
		{
			_ERROR("OBSE version too old (got %08X expected at least %08X)", obse->obseVersion, OBSE_VERSION_INTEGER);
			return false;
		}

		if(obse->oblivionVersion != OBLIVION_VERSION)
		{
			_ERROR("incorrect Oblivion version (got %08X need %08X)", obse->oblivionVersion, OBLIVION_VERSION);
			return false;
		}

	}
	else
	{
		//return false; // not using the editor
	}

	// version checks pass

	return true;
}


OBSEMessagingInterface * g_messageInterface;

void MessageHandler(OBSEMessagingInterface::Message * msg)
{
	switch (msg->type)
	{
	case OBSEMessagingInterface::kMessage_PostLoadGame:
		g_inDialogueMenu = 0;
		break;
	case OBSEMessagingInterface::kMessage_PostLoad:
		WriteRelJump(kShadowsHook,(UInt32)UpdateShadows);
		break;
	}
}


bool OBSEPlugin_Load(const OBSEInterface * obse)
{
	//_MESSAGE("load");

	g_pluginHandle = obse->GetPluginHandle();


	// Load ini settings
	char filename[MAX_PATH];
	char * pch;
	GetModuleFileNameA(NULL, filename, MAX_PATH);
	pch = strrchr(filename, '\\') + 1;
	strcpy(pch, inipath);

	//_MESSAGE("filepath:");
	//_MESSAGE(filename);

	g_bEnableFirstPersonBody = GetPrivateProfileIntA("Main", "bEnableFirstPersonBody", 1, filename);
	g_bEnableHeadBob = GetPrivateProfileIntA("Main", "bEnableHeadBob", 0, filename);
	g_bFirstPersonShadows = GetPrivateProfileIntA("Main", "bFirstPersonShadows", 0, filename);
	g_bVisibleArmsWeaponSheathed = GetPrivateProfileIntA("Main", "bVisibleArmsWeaponSheathed", 1, filename);
	g_bHideShieldWeaponSheathed = GetPrivateProfileIntA("Main", "bHideShieldWeaponSheathed", 1, filename);
	g_bFirstPersonSitting = GetPrivateProfileIntA("Main", "bFirstPersonSitting", 1, filename);
	g_bFirstPersonKnockout = GetPrivateProfileIntA("Main", "bFirstPersonKnockout", 1, filename);
	g_bFirstPersonDeath = GetPrivateProfileIntA("Main", "bFirstPersonDeath", 1, filename);
	g_bFirstPersonModAnim = GetPrivateProfileIntA("Main", "bFirstPersonModAnim", 1, filename);
	g_bFirstPersonAnimCameraRotation = GetPrivateProfileIntA("Main", "bFirstPersonAnimCameraRotation", 1, filename);
	g_bPreventChaseCamDistanceReset = GetPrivateProfileIntA("Main", "bPreventChaseCamDistanceReset", 1, filename);
	g_bEnableHeadFirstPerson = GetPrivateProfileIntA("Main", "bEnableHeadFirstPerson", 0, filename);
	g_bHorseFirstPersonBody = GetPrivateProfileIntA("Main", "bHorseFirstPersonBody", 0, filename);
	g_bUseThirdPersonArms = GetPrivateProfileIntA("Main", "bUseThirdPersonArms", 0, filename);
	g_bRotateThirdPersonArms = GetPrivateProfileIntA("Main", "bRotateThirdPersonArms", 0, filename);

	char resultString[255];
	GetPrivateProfileString("Main", "fSittingMaxLookingDownOverride", "40", resultString, 255, filename);
	g_fSittingMaxLookingDownOverride = atof(resultString);
	GetPrivateProfileString("Main", "fMountedMaxLookingDownOverride", "40", resultString, 255, filename);
	g_fMountedMaxLookingDownOverride = atof(resultString);

	GetPrivateProfileString("Main", "fVanityModeForceDefaultOverride", "300", resultString, 255, filename);
	g_fVanityModeForceDefaultOverride = atof(resultString);

	GetPrivateProfileString("Main", "fCameraPosX", "0", resultString, 255, filename);
	g_fCameraPosX = atof(resultString);
	GetPrivateProfileString("Main", "fCameraPosY", "14.0", resultString, 255, filename);
	g_fCameraPosY = atof(resultString);
	GetPrivateProfileString("Main", "fCameraPosZ", "6.0", resultString, 255, filename);
	g_fCameraPosZ = atof(resultString);
	GetPrivateProfileString("Main", "fHorseMountCameraPosY", "6.0", resultString, 255, filename);
	g_fHorseMountCameraPosY = atof(resultString);
	GetPrivateProfileString("Main", "fVampireFeedCameraPosY", "6.0", resultString, 255, filename);
	g_fVampireFeedCameraPosY = atof(resultString);
	GetPrivateProfileString("Main", "fCameraZOffset", "12.0", resultString, 255, filename);
	g_fCameraZOffset = atof(resultString);
	GetPrivateProfileString("Main", "fVampireFeedXYOffset", "-16.0", resultString, 255, filename);
	g_fVampireFeedXYOffset = atof(resultString);
	GetPrivateProfileString("Main", "fVampireFeedZOffset", "10.0", resultString, 255, filename);
	g_fVampireFeedZOffset = atof(resultString);

	if(!obse->isEditor)
	{
		// register to receive messages from OBSE
		g_messageInterface = (OBSEMessagingInterface *)obse->QueryInterface(kInterface_Messaging);
		g_messageInterface->RegisterListener(g_pluginHandle, "OBSE", MessageHandler);


		// Hook to switch POV code in Oblivion
		WriteRelJump(kSwitchPOVHook,(UInt32)HookSwitchPOV);

		// Hook the Camera
		WriteRelJump(kCameraHook,(UInt32)HookCamera);

		// Hook to Oblivion's animation code
		// Used to modify the 1st/3rd person skeleton nodes
		WriteRelJump(kAnimationHook,(UInt32)HookAnimation);

		// Hook to Oblivion's head tracking code. It is disabled in 1st person
		// to prevent strange camera movements when near npcs/dead bodies.
		WriteRelJump(kHeadTrackingHook,(UInt32)HookHeadTracking);

		// Fix player can become invisible during force 3rd person
		WriteRelJump(kFixPlayerFadeOutHook,(UInt32)HookFixPlayerFadeOut);

		// Hook used to disable applying the collision object to the animation
		WriteRelJump(kAnimUpdateCollisionHook,(UInt32)HookAnimUpdateCollision);

		// Hook used to disabling particle system update to fix torch flame bug
		WriteRelJump(kAnimUpdateParticleSysHook,(UInt32)HookAnimUpdateParticleSys);

		if (g_bEnableFirstPersonBody) {

			// Forces game to load 3rd person body when loading save game in 1st person
			SafeWrite16(0x00664FC6,0x9090);
			SafeWrite32(0x00664FC8,0x90909090);

			// The switch POV code broke the view switching, this should fix it...
			SafeWrite16(0x00664FFB,0x9090);
			SafeWrite32(0x00664FFD,0x90909090);

			// Fix double sound bug
			WriteRelJump(kFixAnimSoundsHook,(UInt32)HookFixAnimSounds);

			// Fix the bug where magic shaders are only applied in 3rd person
			WriteRelJump(kFixMagicShader01Hook,(UInt32)HookFixMagicShader01); // body 1
			WriteRelJump(kFixMagicShader02Hook,(UInt32)HookFixMagicShader02); // body 2
			WriteRelJump(kFixMagicShader03Hook,(UInt32)HookFixMagicShader03); // weapon 1
			WriteRelJump(kFixMagicShader04Hook,(UInt32)HookFixMagicShader04); // weapon 2
			WriteRelJump(kFixMagicShader05Hook,(UInt32)HookFixMagicShader05); // PMS
		}


		// Hooks to where the game force switches to 3rd person,
		// enables a fake first person.
		WriteRelJump(kForcePOV01Hook,(UInt32)HookForcePOV01); // chair, fatigue knockout, paralysis
		if (g_bFirstPersonSitting) {
			WriteRelJump(kForcePOV02Hook,(UInt32)HookForcePOV02); // mount horse
			WriteRelJump(kForcePOV03Hook,(UInt32)HookForcePOV03); // dismount horse
			WriteRelJump(kForcePOV04Hook,(UInt32)HookForcePOV04); // vampire feed
		}
		if (g_bFirstPersonDeath) {
			WriteRelJump(kForcePOV05Hook,(UInt32)HookForcePOV05); // death
		}

		// Prevents chase cam distance from being reset during certain animations
		if (g_bPreventChaseCamDistanceReset) {
			WriteRelJump(kFixForcePOVZoomHook,(UInt32)HookFixForcePOVZoom);
			WriteRelJump(kFixHorseMountZoomHook,(UInt32)HookFixHorseMountZoom);
			WriteRelJump(kFixHorseDismountZoomHook,(UInt32)HookFixHorseDismountZoom);
			WriteRelJump(kFixVampireFeedZoomHook,(UInt32)HookFixVampireFeedZoom);
		}


		// Enable first person shadows
		if (g_bFirstPersonShadows) {
			SafeWrite8(0x00407519,0xEB);
		}

		// Override sitting/mounted max looking down angle
		if (g_fSittingMaxLookingDownOverride != 40) {
			SafeWrite32(0x009E8192,(UInt32)&g_fSittingMaxLookingDownOverride);
		}
		if (g_fMountedMaxLookingDownOverride != 40) {
			SafeWrite32(0x009E8162,(UInt32)&g_fMountedMaxLookingDownOverride);
		}

		// Override 3rd person force zoom distance setting
		if (g_fVanityModeForceDefaultOverride != 300) {
			SafeWrite32(0x009E7DB2,(UInt32)&g_fVanityModeForceDefaultOverride);
		}
	}

	return true;
}

};

