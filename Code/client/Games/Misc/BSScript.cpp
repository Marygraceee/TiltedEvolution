#include <Misc/BSScript.h>
#include <Misc/NativeFunction.h>

#include <World.h>
#include <Events/PapyrusFunctionRegisterEvent.h>
#include <Forms/TESForm.h>
#include <Forms/TESQuest.h>
#include <Misc/GameVM.h>
#include <Actor.h>
#include <PlayerCharacter.h>
#include <Games/ActorExtension.h>
#include <Games/PapyrusFunctions.h>
#include <Games/Overrides.h>
#include <Messages/RequestQuestUpdate.h>
#include <Services/PapyrusService.h>
#include <Services/QuestService.h>
#include <Structs/QuestLog.h>

#include <cstring>
#include <limits>

TP_THIS_FUNCTION(TRegisterPapyrusFunction, void, BSScript::IVirtualMachine, NativeFunction*);
TP_THIS_FUNCTION(TBindEverythingToScript, void, BSScript::IVirtualMachine*);
TP_THIS_FUNCTION(TSignaturesMatch, bool, BSScript::NativeFunction, BSScript::NativeFunction*);
TP_THIS_FUNCTION(TCompareVariables, int64_t, void, BSScript::Variable*, BSScript::Variable*);

TRegisterPapyrusFunction* RealRegisterPapyrusFunction = nullptr;
TBindEverythingToScript* RealBindEverythingToScript = nullptr;
TSignaturesMatch* RealSignaturesMatch = nullptr;
TCompareVariables* RealCompareVariables = nullptr;

using TSetObjectiveDisplayed = void(__fastcall*)(BSScript::IVirtualMachine*, uint32_t, const TESQuest*, int32_t, bool, bool);
using TSetObjectiveCompleted = void(__fastcall*)(BSScript::IVirtualMachine*, uint32_t, const TESQuest*, int32_t, bool);
using TSetObjectiveFailed = void(__fastcall*)(BSScript::IVirtualMachine*, uint32_t, const TESQuest*, int32_t, bool);
using TQuestObjectiveReset = void(__fastcall*)(BSScript::IVirtualMachine*, uint32_t, const TESQuest*);

static TSetObjectiveDisplayed RealSetObjectiveDisplayed = nullptr;
static TSetObjectiveCompleted RealSetObjectiveCompleted = nullptr;
static TSetObjectiveFailed RealSetObjectiveFailed = nullptr;
static TQuestObjectiveReset RealQuestReset = nullptr;
static TQuestObjectiveReset RealCompleteQuest = nullptr;
static TQuestObjectiveReset RealCompleteAllObjectives = nullptr;
static TQuestObjectiveReset RealFailAllObjectives = nullptr;

static void QueueObjectiveUpdate(const TESQuest* apQuest, const int32_t aObjectiveIndex)
{
    if (!apQuest || ScopedQuestOverride::IsOverriden() || aObjectiveIndex < -1 || aObjectiveIndex > std::numeric_limits<uint16_t>::max())
        return;

    World::Get().GetRunner().Queue(
        [formId = apQuest->formID, objectiveIndex = aObjectiveIndex]()
        {
            World& world = World::Get();
            if (!world.GetTransport().IsOnline() || !world.GetPartyService().IsInParty())
                return;

            auto* pQuest = Cast<TESQuest>(TESForm::GetById(formId));
            if (!pQuest || QuestService::IsNonSyncableQuest(pQuest))
                return;

            auto& papyrus = world.ctx().at<PapyrusService>();
            const void* isObjectiveDisplayedAddress = papyrus.Get("Quest", "IsObjectiveDisplayed");
            const void* isObjectiveCompletedAddress = papyrus.Get("Quest", "IsObjectiveCompleted");
            const void* isObjectiveFailedAddress = papyrus.Get("Quest", "IsObjectiveFailed");
            if (!isObjectiveDisplayedAddress || !isObjectiveCompletedAddress || !isObjectiveFailedAddress)
                return;

            const PapyrusFunction<bool, TESQuest, int32_t> isObjectiveDisplayed(isObjectiveDisplayedAddress);
            const PapyrusFunction<bool, TESQuest, int32_t> isObjectiveCompleted(isObjectiveCompletedAddress);
            const PapyrusFunction<bool, TESQuest, int32_t> isObjectiveFailed(isObjectiveFailedAddress);

            GameId questId;
            if (!world.GetModSystem().GetServerModId(formId, questId))
                return;

            auto sendObjective = [&](const uint16_t index)
            {
                uint8_t flags = 0;
                if (isObjectiveDisplayed(pQuest, index))
                    flags |= QuestLog::ObjectiveDisplayed;
                if (isObjectiveCompleted(pQuest, index))
                    flags |= QuestLog::ObjectiveCompleted;
                if (isObjectiveFailed(pQuest, index))
                    flags |= QuestLog::ObjectiveFailed;

                RequestQuestUpdate update{};
                update.Id = questId;
                update.Stage = index;
                update.Status = RequestQuestUpdate::ObjectiveUpdate;
                update.ClientQuestType = flags;
                world.GetTransport().Send(update);
            };

            if (objectiveIndex == -1)
            {
                for (const auto& objective : pQuest->objectives)
                {
                    if (objective)
                        sendObjective(objective->stageId);
                }
            }
            else
                sendObjective(static_cast<uint16_t>(objectiveIndex));
        });
}

static void __fastcall
HookSetObjectiveDisplayed(BSScript::IVirtualMachine* apVirtualMachine, uint32_t aStackId, const TESQuest* apQuest, int32_t aObjectiveIndex, bool aDisplayed, bool aForce)
{
    RealSetObjectiveDisplayed(apVirtualMachine, aStackId, apQuest, aObjectiveIndex, aDisplayed, aForce);
    QueueObjectiveUpdate(apQuest, aObjectiveIndex);
}

static void __fastcall HookSetObjectiveCompleted(BSScript::IVirtualMachine* apVirtualMachine, uint32_t aStackId, const TESQuest* apQuest, int32_t aObjectiveIndex, bool aCompleted)
{
    RealSetObjectiveCompleted(apVirtualMachine, aStackId, apQuest, aObjectiveIndex, aCompleted);
    QueueObjectiveUpdate(apQuest, aObjectiveIndex);
}

static void __fastcall HookSetObjectiveFailed(BSScript::IVirtualMachine* apVirtualMachine, uint32_t aStackId, const TESQuest* apQuest, int32_t aObjectiveIndex, bool aFailed)
{
    RealSetObjectiveFailed(apVirtualMachine, aStackId, apQuest, aObjectiveIndex, aFailed);
    QueueObjectiveUpdate(apQuest, aObjectiveIndex);
}

static void __fastcall HookQuestReset(BSScript::IVirtualMachine* apVirtualMachine, uint32_t aStackId, const TESQuest* apQuest)
{
    RealQuestReset(apVirtualMachine, aStackId, apQuest);
    QueueObjectiveUpdate(apQuest, -1);
}

static void __fastcall HookCompleteQuest(BSScript::IVirtualMachine* apVirtualMachine, uint32_t aStackId, const TESQuest* apQuest)
{
    RealCompleteQuest(apVirtualMachine, aStackId, apQuest);
    QueueObjectiveUpdate(apQuest, -1);
}

static void __fastcall HookCompleteAllObjectives(BSScript::IVirtualMachine* apVirtualMachine, uint32_t aStackId, const TESQuest* apQuest)
{
    RealCompleteAllObjectives(apVirtualMachine, aStackId, apQuest);
    QueueObjectiveUpdate(apQuest, -1);
}

static void __fastcall HookFailAllObjectives(BSScript::IVirtualMachine* apVirtualMachine, uint32_t aStackId, const TESQuest* apQuest)
{
    RealFailAllObjectives(apVirtualMachine, aStackId, apQuest);
    QueueObjectiveUpdate(apQuest, -1);
}

void TP_MAKE_THISCALL(HookRegisterPapyrusFunction, BSScript::IVirtualMachine, NativeFunction* apFunction)
{
    auto& runner = World::Get().GetRunner();

    const char* functionName = apFunction->functionName.AsAscii();
    const char* namespaceName = apFunction->typeName.AsAscii();
    PapyrusFunctionRegisterEvent event(functionName, namespaceName, apFunction->functionAddress);

    runner.Trigger(std::move(event));

    if (std::strcmp(namespaceName, "Quest") == 0)
    {
        if (std::strcmp(functionName, "SetObjectiveDisplayed") == 0)
        {
            RealSetObjectiveDisplayed = reinterpret_cast<TSetObjectiveDisplayed>(apFunction->functionAddress);
            apFunction->functionAddress = reinterpret_cast<void*>(&HookSetObjectiveDisplayed);
        }
        else if (std::strcmp(functionName, "SetObjectiveCompleted") == 0)
        {
            RealSetObjectiveCompleted = reinterpret_cast<TSetObjectiveCompleted>(apFunction->functionAddress);
            apFunction->functionAddress = reinterpret_cast<void*>(&HookSetObjectiveCompleted);
        }
        else if (std::strcmp(functionName, "SetObjectiveFailed") == 0)
        {
            RealSetObjectiveFailed = reinterpret_cast<TSetObjectiveFailed>(apFunction->functionAddress);
            apFunction->functionAddress = reinterpret_cast<void*>(&HookSetObjectiveFailed);
        }
        else if (std::strcmp(functionName, "Reset") == 0)
        {
            RealQuestReset = reinterpret_cast<TQuestObjectiveReset>(apFunction->functionAddress);
            apFunction->functionAddress = reinterpret_cast<void*>(&HookQuestReset);
        }
        else if (std::strcmp(functionName, "CompleteQuest") == 0)
        {
            RealCompleteQuest = reinterpret_cast<TQuestObjectiveReset>(apFunction->functionAddress);
            apFunction->functionAddress = reinterpret_cast<void*>(&HookCompleteQuest);
        }
        else if (std::strcmp(functionName, "CompleteAllObjectives") == 0)
        {
            RealCompleteAllObjectives = reinterpret_cast<TQuestObjectiveReset>(apFunction->functionAddress);
            apFunction->functionAddress = reinterpret_cast<void*>(&HookCompleteAllObjectives);
        }
        else if (std::strcmp(functionName, "FailAllObjectives") == 0)
        {
            RealFailAllObjectives = reinterpret_cast<TQuestObjectiveReset>(apFunction->functionAddress);
            apFunction->functionAddress = reinterpret_cast<void*>(&HookFailAllObjectives);
        }
    }

    TiltedPhoques::ThisCall(RealRegisterPapyrusFunction, apThis, apFunction);
}

void TP_MAKE_THISCALL(HookBindEverythingToScript, BSScript::IVirtualMachine*)
{
    (*apThis)->BindNativeMethod(new BSScript::IsRemotePlayerFunc("IsRemotePlayer", "SkyrimTogetherUtils", PapyrusFunctions::IsRemotePlayer, BSScript::Variable::kBoolean));
    (*apThis)->BindNativeMethod(new BSScript::IsPlayerFunc("IsPlayer", "SkyrimTogetherUtils", PapyrusFunctions::IsPlayer, BSScript::Variable::kBoolean));
    (*apThis)->BindNativeMethod(new BSScript::DidLaunchSkyrimTogetherFunc("DidLaunchSkyrimTogether", "SkyrimTogetherVerifyLaunchScript", PapyrusFunctions::DidLaunchSkyrimTogether, BSScript::Variable::kBoolean));

    TiltedPhoques::ThisCall(RealBindEverythingToScript, apThis);
}

bool TP_MAKE_THISCALL(HookSignaturesMatch, BSScript::NativeFunction, BSScript::NativeFunction* apOther)
{
    /*
    if (!strcmp(apThis->GetName().AsAscii(), "IsRemotePlayer"))
        DebugBreak();
    */

    return TiltedPhoques::ThisCall(RealSignaturesMatch, apThis, apOther);
}

// This is a neat hack, but it has been disabled since it messes up other things like beastform.
// These kinds of issues should be solved with custom scripts now that we have SkyrimTogether.esp anyway.
int64_t TP_MAKE_THISCALL(HookCompareVariables, void, BSScript::Variable* apVar1, BSScript::Variable* apVar2)
{
    BSScript::Object* pObject1 = apVar1->GetObject();
    BSScript::Object* pObject2 = apVar2->GetObject();

    if (!pObject1 || !pObject2)
        return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);

    uint64_t handle1 = pObject1->GetHandle();
    uint64_t handle2 = pObject2->GetHandle();

    auto* pPolicy = GameVM::Get()->virtualMachine->GetObjectHandlePolicy();

    if (!pPolicy || !handle1 || !handle2 || !pPolicy->HandleIsType((uint32_t)Actor::Type, handle1) || !pPolicy->HandleIsType((uint32_t)Actor::Type, handle2) || !pPolicy->IsHandleObjectAvailable(handle1) || !pPolicy->IsHandleObjectAvailable(handle2))
    {
        return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);
    }

    Actor* pActor1 = pPolicy->GetObjectForHandle<Actor>(handle1);
    Actor* pActor2 = pPolicy->GetObjectForHandle<Actor>(handle2);

    if (!pActor1 || !pActor2)
        return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);

    if (pActor1 == PlayerCharacter::Get())
    {
        auto* pExtension = pActor2->GetExtension();
        if (pExtension && pExtension->IsPlayer())
            return 0;
    }
    else if (pActor2 == PlayerCharacter::Get())
    {
        auto* pExtension = pActor1->GetExtension();
        if (pExtension && pExtension->IsPlayer())
            return 0;
    }

    return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);
}

static TiltedPhoques::Initializer s_vmHooks(
    []()
    {
        POINTER_SKYRIMSE(TRegisterPapyrusFunction, s_registerPapyrusFunction, 104788);
        POINTER_SKYRIMSE(TBindEverythingToScript, s_bindEverythingToScript, 55739);
        POINTER_SKYRIMSE(TSignaturesMatch, s_signaturesMatch, 104359);

        // POINTER_SKYRIMSE(TCompareVariables, s_compareVariables, 105220);

        RealRegisterPapyrusFunction = s_registerPapyrusFunction.Get();
        RealBindEverythingToScript = s_bindEverythingToScript.Get();
        RealSignaturesMatch = s_signaturesMatch.Get();
        // RealCompareVariables = s_compareVariables.Get();

        TP_HOOK(&RealRegisterPapyrusFunction, HookRegisterPapyrusFunction);
        TP_HOOK(&RealBindEverythingToScript, HookBindEverythingToScript);
        TP_HOOK(&RealSignaturesMatch, HookSignaturesMatch);
        // TP_HOOK(&RealCompareVariables, HookCompareVariables);
    });
