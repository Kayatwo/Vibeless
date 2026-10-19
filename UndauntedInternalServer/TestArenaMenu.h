#pragma once
#include "TestArena.h"
#include <array>

namespace TestArenaMenu {
using namespace SDK;
inline UWorld* World=nullptr;
inline AArchonPlayerController* Player=nullptr;
inline UUserWidget* Menu=nullptr;
inline UUserWidget* Hint=nullptr;
inline UComboBoxString* Selection=nullptr;
inline UTextBlock* Status=nullptr;
inline std::array<UButton*,4> Buttons{};
inline std::array<bool,4> Pressed{};
inline std::vector<TestArena::Creature> Entries;
inline int PlayerIndex=-1,MenuIndex=-1,HintIndex=-1;
inline bool Open=false,OldCursor=false;
inline ULONGLONG RetryAt=0;
inline bool Alive(UObject* object,int index) {return object && index>=0 && UObject::GObjects->GetByIndex(index)==object;}
template<class T> T* New(UObject* outer) {return static_cast<T*>(UGameplayStatics::SpawnObject(T::StaticClass(),outer));}
inline UTextBlock* Label(UWidgetTree* tree,const std::string& value,int size=20) {
    auto* text=New<UTextBlock>(tree);
    if(!text) return nullptr;
    auto font=text->Font; font.Size=size; text->SetFont(font);
    text->SetText(TestArena::Text(value)); text->SetAutoWrapText(true);
    return text;
}
inline void Show(bool open) {
    if(!Alive(Player,PlayerIndex) || !Alive(Menu,MenuIndex) || Open==open) return;
    Open=open; Pressed.fill(false);
    Menu->SetVisibility(open?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    Player->SetIgnoreMoveInput(open); Player->SetIgnoreLookInput(open);
    if(open) {
        OldCursor=Player->bShowMouseCursor; Player->bShowMouseCursor=true;
        UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(Player,Selection,EMouseLockMode::DoNotLock,false);
        Selection->SetKeyboardFocus();
    } else {
        Player->bShowMouseCursor=OldCursor;
        UWidgetBlueprintLibrary::SetInputMode_GameOnly(Player);
    }
}
inline void Reset() {
    if(Open) Show(false);
    if(Alive(Menu,MenuIndex)) Menu->RemoveFromParent();
    if(Alive(Hint,HintIndex)) Hint->RemoveFromParent();
    Menu=nullptr; Hint=nullptr; Player=nullptr; Selection=nullptr; Status=nullptr;
    Buttons.fill(nullptr); Pressed.fill(false); Entries.clear();
    PlayerIndex=MenuIndex=HintIndex=-1; Open=false;
}
inline UUserWidget* Host(UWorld* world,AArchonPlayerController* player) {
    // Use a concrete stock UMG host (UUserWidget itself is abstract). Replace
    // its tree before Slate builds it; authored child bindings remain valid.
    const auto path=UKismetSystemLibrary::MakeSoftClassPath(FString(L"/Game/UI/HuntInfo/huntinfo_current_loadout_rec_bpw.huntinfo_current_loadout_rec_bpw_C"));
    auto* type=UKismetSystemLibrary::LoadClassAsset_Blocking(UKismetSystemLibrary::Conv_SoftClassPathToSoftClassRef(path));
    if(!type) return nullptr;
    auto* host=UWidgetBlueprintLibrary::Create(world,type,player);
    if(host) host->WidgetTree=New<UWidgetTree>(host);
    return host && host->WidgetTree ? host : nullptr;
}
inline bool Build(UWorld* world,AArchonPlayerController* player) {
    Entries=TestArena::Catalog(world);
    if(Entries.empty()) return false;
    Menu=Host(world,player); if(!Menu) return false;
    MenuIndex=Menu->Index; Player=player; PlayerIndex=player->Index;
    auto* tree=Menu->WidgetTree;
    auto* canvas=New<UCanvasPanel>(tree); auto* border=New<UBorder>(tree); auto* body=New<UVerticalBox>(tree);
    if(!canvas || !border || !body) {Reset();return false;}
    tree->RootWidget=canvas;
    border->SetBrushColor(FLinearColor{0.025f,0.035f,0.055f,0.98f}); border->SetPadding(FMargin{24,20,24,20});
    border->AddChild(body);
    auto* slot=canvas->AddChildToCanvas(border);
    slot->SetAnchors(FAnchors{{0.5f,0.5f},{0.5f,0.5f}});slot->SetAlignment({0.5f,0.5f});slot->SetSize({660,490});
    body->AddChildToVerticalBox(Label(tree,"TEST ARENA",28))->SetPadding({0,0,0,12});
    body->AddChildToVerticalBox(Label(tree,"Choose a creature. Face clear ground before spawning. Up to three can be alive at once.",18))->SetPadding({0,0,0,14});
    Selection=New<UComboBoxString>(tree);
    if(!Selection) {Reset();return false;}
    Selection->MaxListHeight=360; Selection->EnableGamepadNavigationMode=true;
    auto font=Selection->Font;font.Size=18;Selection->Font=font;
    for(const auto& entry:Entries) {
        const auto label=TestArena::Text(entry.Label).ToString();
        const int length=MultiByteToWideChar(CP_UTF8,0,label.data(),static_cast<int>(label.size()),nullptr,0);
        std::wstring wide(length,L'\0');if(length)MultiByteToWideChar(CP_UTF8,0,label.data(),static_cast<int>(label.size()),wide.data(),length);
        Selection->AddOption(FString(wide.c_str()));
    }
    Selection->SetSelectedIndex(0);
    body->AddChildToVerticalBox(Selection)->SetPadding({0,0,0,12});
    const char* names[]={"Spawn selected creature","Despawn selected species","Clear arena","Close controls"};
    for(size_t i=0;i<Buttons.size();++i) {
        Buttons[i]=New<UButton>(tree);
        if(!Buttons[i]) {Reset();return false;}
        Buttons[i]->AddChild(Label(tree,names[i]));
        body->AddChildToVerticalBox(Buttons[i])->SetPadding({0,3,0,3});
    }
    Status=Label(tree,"F8 toggles these controls. The pause menu returns you to Ramsgate.",16);
    body->AddChildToVerticalBox(Status)->SetPadding({0,14,0,0});
    Menu->AddToViewport(1000); Menu->SetVisibility(ESlateVisibility::Collapsed);
    Hint=Host(world,player);
    if(Hint) {
        HintIndex=Hint->Index;
        auto* hintCanvas=New<UCanvasPanel>(Hint->WidgetTree);
        Hint->WidgetTree->RootWidget=hintCanvas;
        auto* hintText=Label(Hint->WidgetTree,"TEST ARENA  |  F8: Creature controls",18);
        auto* hintSlot=hintCanvas->AddChildToCanvas(hintText);hintSlot->SetPosition({28,100});hintSlot->SetSize({500,40});
        Hint->AddToViewport(990);Hint->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    Show(true);
    return true;
}
inline void Send(size_t action) {
    if(action==3) {Show(false);return;}
    const int selected=Selection->GetSelectedIndex();
    if(selected<0 || static_cast<size_t>(selected)>=Entries.size()) return;
    std::string command(TestArenaPolicy::Prefix);
    command+= action==0 ? "spawn|"+Entries[selected].Id : action==1 ? "despawn|"+Entries[selected].Id : "clear";
    const std::wstring wide(command.begin(),command.end());
    Status->SetText(TestArena::Text(std::string("Waiting for the arena...")));
    Player->ServerCheat(FString(wide.c_str()));
}
inline void Tick(UWorld* world) {
    if(world!=World || !TestArena::IsWorld(world)) {Reset();World=world;RetryAt=0;}
    if(!TestArena::IsWorld(world) || !world->NetDriver || !world->NetDriver->ServerConnection) return;
    auto* player=UArchonGameplayStatics::GetArchonLocalPlayerController(world);
    if(!player || !player->Pawn || !HuntWorld::State(world) || !HuntWorld::State(world)->GameStarted) return;
    if(!Alive(Menu,MenuIndex) || player!=Player) {
        if(GetTickCount64()<RetryAt) return;
        Reset();RetryAt=GetTickCount64()+5000;
        Build(world,player);return;
    }
    FKey toggle{};toggle.KeyName=UKismetStringLibrary::Conv_StringToName(FString(L"F8"));
    if(player->WasInputKeyJustPressed(toggle)) Show(!Open);
    if(!Open) return;
    // Poll Slate's pressed state on the game thread, including keyboard/gamepad
    // activation, without repurposing unrelated Blueprint event delegates.
    for(size_t i=0;i<Buttons.size();++i) {
        const bool pressed=Buttons[i]->IsPressed();
        if(pressed && !Pressed[i]) Send(i);
        Pressed[i]=pressed;
    }
}
inline bool BeforeEvent(UObject* object,UFunction* function,void* parameters) {
    if(object!=Player || !parameters || !function || function->GetName()!="ClientMessage"
        || !Alive(Menu,MenuIndex) || !TestArena::IsWorld(UWorld::GetWorld())) return false;
    const auto& message=static_cast<Params::PlayerController_ClientMessage*>(parameters)->S;
    if(!message.IsValid() || message.Num()<1 || message.Num()>512) return false;
    const auto text=message.ToString();
    if(!text.starts_with(TestArenaPolicy::Prefix)) return false;
    if(Status) Status->SetText(TestArena::Text(text.substr(TestArenaPolicy::Prefix.size())));
    return true;
}
}
