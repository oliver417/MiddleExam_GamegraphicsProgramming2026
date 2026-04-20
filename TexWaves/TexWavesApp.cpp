//***************************************************************************************
// TexWavesApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "../../Common/Camera.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include "FrameResource.h"
#include "Waves.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
	Opaque = 0,
	Transparent,
	AlphaTest,
	Count
};

// 헬프 마커 유틸리티 함수 (ImGui 데모 코드에서 가져옴)
static void HelpMarker(const char* desc)
{
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

class TexWavesApp : public D3DApp
{
public:
	TexWavesApp(HINSTANCE hInstance);
	TexWavesApp(const TexWavesApp& rhs) = delete;
	TexWavesApp& operator=(const TexWavesApp& rhs) = delete;
	~TexWavesApp();

	virtual bool Initialize()override;

	virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)override; // ImGui 입력 재정의를 위해

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateWaves(const GameTimer& gt);
	void UpdateUI(const GameTimer& gt);
	void UpdateLandVB(const GameTimer& gt);

	void LoadHeightmap(std::string filename);
	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildLandGeometry();
	void BuildWavesGeometry();
	void BuildBoxGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	float GetHillsHeight(float x, float z)const;
	XMFLOAT3 GetHillsNormal(float x, float z)const;

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	UINT mCbvSrvDescriptorSize = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	RenderItem* mWavesRitem = nullptr;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	std::unique_ptr<Waves> mWaves;
	
	UINT mLandVertexCount = 0;

	std::vector<Vertex> mLandVertices; // CPU에서 수정할 정점 배열
	int mLandDirtyFrames = gNumFrameResources; // 지형이 변했을 때 3프레임 동안 업데이트하기 위한 카운터

	PassConstants mMainPassCB;

	float mHeightScale = 1.0f; // grass 기본 지형 높이 배율

	Camera mCamera; // 새롭게 추가할 카메라 객체

	// 고도별 기본 색상
	DirectX::XMFLOAT4 mLowColor = { 0.95f, 0.85f, 0.65f, 1.0f };  // 모래
	DirectX::XMFLOAT4 mMidColor = { 0.48f, 0.77f, 0.46f, 1.0f };  // 풀
	DirectX::XMFLOAT4 mHighColor = { 1.0f, 1.0f, 1.0f, 1.0f };    // 눈

	float mLowThreshold = 2.0f; // 아래 -> 중간 기준점
	float mHighThreshold = 10.0f; // 중간 -> 위 기준점

	std::vector<float> mHeightMap;
	int mHeightMapWidth = 256; // 하이트맵 가로 크기
	int mHeightMapHeight = 256; // 하이트맵 세로 크기
	bool mIsHeightMapLoaded = false;
	bool mUseHeightmap = false; // 하이트맵 적용 여부 체크박스용

	float mSunTheta = 1.25f * DirectX::XM_PI; // 태양의 좌우 각도 (동/서)
	float mSunPhi = DirectX::XM_PIDIV4;       // 태양의 높낮이 각도 (남중고도, 0이면 정수리 위)

	// fence
	RenderItem* mFenceRitem = nullptr;
	DirectX::XMFLOAT3 mFenceBasePos = { 0.0f, 2.5f, 0.0f }; // 울타리가 처음 설치된 위치
	float mFenceMove[3] = { 0.0f, 2.5f, 0.0f }; // XYZ 위치
	float mFenceScale[3] = { 20.0f, 5.0f, 0.1f }; // 가로, 높이, 두께

	enum TimeOfDay { DAY = 0, SUNSET, NIGHT};
	int mTimeOfDay = DAY; // 기본값은 낮으로

	POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		TexWavesApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

TexWavesApp::TexWavesApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

TexWavesApp::~TexWavesApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool TexWavesApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);
	mCamera.SetPosition(0.0f, 20.0f, -50.0f);

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildLandGeometry();
	BuildWavesGeometry();
	BuildBoxGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();


	// ImGui 초기화
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();//윈도우 기본 폰트 사용해서 한글 사용할 수 있게 함(슬라이드 텍스트 등등)
	io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgun.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesKorean());

	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init(MainWnd());
	ImGui_ImplDX12_InitInfo initInfo = {};
	initInfo.Device = md3dDevice.Get();
	initInfo.CommandQueue = mCommandQueue.Get();
	initInfo.NumFramesInFlight = gNumFrameResources;
	initInfo.RTVFormat = mBackBufferFormat;

	// ImGui용 슬롯은 텍스처 3개(0,1,2) 이후인 슬롯 3번으로 지정
	UINT descriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	UINT texCount = (UINT)mTextures.size();

	D3D12_CPU_DESCRIPTOR_HANDLE imguiCpuHandle = mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	imguiCpuHandle.ptr += texCount * descriptorSize;  // 슬롯 3번

	D3D12_GPU_DESCRIPTOR_HANDLE imguiGpuHandle = mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	imguiGpuHandle.ptr += texCount * descriptorSize;  // 슬롯 3번

	initInfo.LegacySingleSrvCpuDescriptor = imguiCpuHandle;
	initInfo.LegacySingleSrvGpuDescriptor = imguiGpuHandle;

	ImGui_ImplDX12_Init(&initInfo);

	return true;
}


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT TexWavesApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// ImGui가 메세지를 먼저 처리할 수 있게
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
		return true;

	return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}

void TexWavesApp::OnResize()
{
	D3DApp::OnResize();

	// 화면 비율에 맞춰 카메라 렌즈 설정
	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void TexWavesApp::Update(const GameTimer& gt)
{
	// ImGui 프레임 시작
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	OnKeyboardInput(gt);
	UpdateCamera(gt);
	UpdateUI(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
	UpdateWaves(gt);
	UpdateLandVB(gt);
}

void TexWavesApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// 시간대에 따라 배경 색상 결정
	float clearColor[4];
	if (mTimeOfDay == DAY)
	{
		// 낮은 밝은 파란색
		clearColor[0] = 0.528f; clearColor[1] = 0.808f; clearColor[2] = 0.922f; clearColor[3] = 1.0f;
	}
	else if (mTimeOfDay == SUNSET)
	{
		// 노을은 붉고 주황빛
		clearColor[0] = 0.8f;   clearColor[1] = 0.4f;   clearColor[2] = 0.2f;   clearColor[3] = 1.0f;
	}
	else // NIGHT
	{
		// 밤은 어두운 남색
		clearColor[0] = 0.05f;  clearColor[1] = 0.05f;  clearColor[2] = 0.1f;   clearColor[3] = 1.0f;
	}		

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), clearColor, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	mCommandList->SetPipelineState(mPSOs["opaque"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs["alphaTest"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTest]);

	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);

	// ImGui 실제 그리기 명령
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));	

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void TexWavesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	// ImGui가 마우스 입력을 사용 중이면 함수 종료
	if (ImGui::GetIO().WantCaptureKeyboard) return;

	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void TexWavesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void TexWavesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if (ImGui::GetIO().WantCaptureKeyboard) return;

	if ((btnState & MK_LBUTTON) != 0)
	{
		// 마우스 이동량 계산
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// 카메라 회전 적용 (Pitch: 상하, RotateY: 좌우)
		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void TexWavesApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();
	float speed = 100.0f * dt; // 이동 속도

	if (GetAsyncKeyState('W') & 0x8000) mCamera.Walk(speed);
	if (GetAsyncKeyState('S') & 0x8000) mCamera.Walk(-speed);
	if (GetAsyncKeyState('A') & 0x8000) mCamera.Strafe(-speed);
	if (GetAsyncKeyState('D') & 0x8000) mCamera.Strafe(speed);
}

void TexWavesApp::UpdateCamera(const GameTimer& gt)
{
	OnKeyboardInput(gt); // 카메라 이동을 위한 키보드 입력 호출

	mCamera.UpdateViewMatrix(); // 카메라가 가지고 있는 뷰 행렬 최신화
}

void TexWavesApp::AnimateMaterials(const GameTimer& gt)
{
	// Scroll the water material texture coordinates.
	auto waterMat = mMaterials["water"].get();	

	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);	
	

	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();

	if (tu >= 1.0f)
		tu += 1.0f;

	if (tv >= 1.0f)
		tv -= 1.0f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;
	
	// Material has changed, so need to update cbuffer.
	waterMat->NumFramesDirty = gNumFrameResources;
}

void TexWavesApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void TexWavesApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for (auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void TexWavesApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	// 버퍼에 담을 구조체 선언
	PassConstants passConstants;

	// 매트릭스 대입 코드
	XMStoreFloat4x4(&passConstants.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&passConstants.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&passConstants.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&passConstants.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&passConstants.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&passConstants.InvViewProj, XMMatrixTranspose(invViewProj));
	passConstants.EyePosW = mCamera.GetPosition3f();
	passConstants.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	passConstants.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	passConstants.NearZ = 1.0f;
	passConstants.FarZ = 1000.0f;
	passConstants.TotalTime = gt.TotalTime();
	passConstants.DeltaTime = gt.DeltaTime();

	// 시간에 따른 조명 세팅
	DirectX::XMFLOAT4 ambientColor;
	DirectX::XMFLOAT3 sunColor;

	if (mTimeOfDay == DAY)
	{
		mSunPhi = DirectX::XM_PIDIV4; // 낮
		ambientColor = { 0.25f, 0.25f, 0.35f, 1.0f };
		sunColor = { 1.0f, 0.96f, 0.85f };
	}
	else if (mTimeOfDay == SUNSET)
	{
		mSunPhi = DirectX::XM_PI / 2.0f - 0.1f; // 노을
		ambientColor = { 0.15f, 0.1f, 0.1f, 1.0f };
		sunColor = { 1.0f, 0.4f, 0.1f };
	}
	else // NIGHT
	{
		mSunPhi = DirectX::XM_PI - 0.2f; // 밤
		ambientColor = { 0.05f, 0.05f, 0.1f, 1.0f };
		sunColor = { 0.1f, 0.2f, 0.4f };
	}

	// 배경광 적용
	passConstants.AmbientLight = ambientColor;

	// 태양의 방향을 각도를 이용해 3D 벡터로 변환
	DirectX::XMVECTOR lightDir = -MathHelper::SphericalToCartesian(1.0f, mSunTheta, mSunPhi);
	DirectX::XMStoreFloat3(&passConstants.Lights[0].Direction, lightDir);
	passConstants.Lights[0].Strength = sunColor; // 태양/달빛 색상 적용

	// 보조광 설정 (그림자가 진해지는 것을 방지)
	passConstants.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	passConstants.Lights[1].Strength = { 0.1f, 0.1f, 0.1f };
	passConstants.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	passConstants.Lights[2].Strength = { 0.0f, 0.0f, 0.0f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, passConstants);
}

void TexWavesApp::UpdateWaves(const GameTimer& gt)
{
	// Every quarter second, generate a random wave.
	static float t_base = 0.0f;
	if ((mTimer.TotalTime() - t_base) >= 0.25f)
	{
		t_base += 0.25f;

		int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
		int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);

		float r = MathHelper::RandF(0.2f, 0.5f);

		mWaves->Disturb(i, j, r);
	}

	// Update the wave simulation.
	mWaves->Update(gt.DeltaTime());

	// Update the wave vertex buffer with the new solution.
	auto currWavesVB = mCurrFrameResource->WavesVB.get();
	for (int i = 0; i < mWaves->VertexCount(); ++i)
	{
		Vertex v;

		v.Pos = mWaves->Position(i);
		v.Normal = mWaves->Normal(i);

		// Derive tex-coords from position by 
		// mapping [-w/2,w/2] --> [0,1]
		v.TexC.x = 0.5f + v.Pos.x / mWaves->Width();
		v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();

		v.Color = XMFLOAT4(0.0f, 0.4f, 0.8f, 0.5f);

		currWavesVB->CopyData(i, v);
	}

	// Set the dynamic VB of the wave renderitem to the current frame VB.
	mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void TexWavesApp::UpdateUI(const GameTimer& gt)
{
	// Imgui 오른쪽 위 고정시키기
	// 현재 화면의 크기 가져와서 오른 쪽 위 여백 좌표 계산
	ImVec2 windowPos = ImVec2(ImGui::GetIO().DisplaySize.x - 10.0f, 10.0f);

	// 창의 위치 피벗을 오른쪽 위 모서리로 설정
	ImVec2 windowPosPivot = ImVec2(1.0f, 0.0f);

	// 다음 만들어질 창을 무조건(Always) 해당 위치로 고정
	ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, windowPosPivot);
	// 창을 생성할 때 움직이지 못하게(Nomove) 하고, 내용물에 맞춰 크기 자동조절(alwaysAutoResize)
	ImGui::Begin(u8"맵 에디터", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

	if (ImGui::BeginTabBar(u8"맵 에디터 탭"))
	{
		// 지형 탭
		if (ImGui::BeginTabItem(u8" 지형 "))
		{
			ImGui::Text(u8" 지형 높이 & 고도 조절");
			ImGui::PushItemWidth(180.0f);

			// 하이트맵이 로드되어 있고, 체크박스가 켜져있는지 확인
			bool isHeightmapActive = (mUseHeightmap && mIsHeightMapLoaded);

			// 하이트맵이 켜져있으면 아래의 UI를 비활성화 함
			ImGui::BeginDisabled(isHeightmapActive);

			// 슬라이더 세팅
			if (ImGui::SliderFloat(u8" 지형 높이", &mHeightScale, 0.0f, 5.0f))
				mLandDirtyFrames = gNumFrameResources; //슬라이더가 움직이면 프레임 리소스를 모두 업데이트하도록 설정

			ImGui::EndDisabled(); // 비활성화 영역 종료

			if (ImGui::SliderFloat(u8"모래 지형 라인", &mLowThreshold, -5.0f, mHighThreshold))
				mLandDirtyFrames = gNumFrameResources;

			if (ImGui::SliderFloat(u8"눈 지형 라인", &mHighThreshold, mLowThreshold, 30.0f))
				mLandDirtyFrames = gNumFrameResources;

			ImGui::PopItemWidth();

			ImGui::Dummy(ImVec2(0.0f, 5.0f)); // 5픽셀만큼 띄우기
			ImGui::Separator();// 구분 선
			ImGui::Dummy(ImVec2(0.0f, 10.0f));

			ImGui::Text(u8" 지형 색상");
			if (ImGui::ColorEdit4(u8" 아래", &mLowColor.x)) mLandDirtyFrames = gNumFrameResources;
			if (ImGui::ColorEdit4(u8" 중간", &mMidColor.x)) mLandDirtyFrames = gNumFrameResources;
			if (ImGui::ColorEdit4(u8" 위", &mHighColor.x)) mLandDirtyFrames = gNumFrameResources;
			ImGui::Dummy(ImVec2(0.0f, 10.0f));

			ImGui::EndTabItem();
		}

		// 하이트맵 탭
		if (ImGui::BeginTabItem(u8" 하이트맵 "))
		{
			// 하이트맵 적용 스위치
			if (ImGui::Checkbox(u8"하이트맵 데이터 사용", &mUseHeightmap))
			{
				mLandDirtyFrames = gNumFrameResources; // 체크 상태가 변하면 지형 업데이트
			}

			ImGui::SameLine();
			HelpMarker(u8"하이트맵 파일이 로드되어 있어야 작동합니다.");
			ImGui::Separator();

			// 파일 로드 버튼
			if (ImGui::Button(u8"RAW File 로드"))
			{
				wchar_t filename[MAX_PATH] = { 0 };
				OPENFILENAMEW ofn;
				ZeroMemory(&ofn, sizeof(ofn));
				ofn.lStructSize = sizeof(ofn);
				ofn.hwndOwner = mhMainWnd;
				ofn.lpstrFilter = L"RAW Files (*.raw)\0*.raw\0All Files (*.*)\0*.*\0";
				ofn.lpstrFile = filename;
				ofn.nMaxFile = MAX_PATH;
				ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
				ofn.lpstrDefExt = L"raw";

				if (GetOpenFileNameW(&ofn)) // 윈도우 창 뜸
				{
					// 와이드 문자열을 string으로 변환하여 로드 함수 호출
					std::wstring ws(filename);
					std::string strPath(ws.begin(), ws.end());
					LoadHeightmap(strPath);
				}
			}

			if (mIsHeightMapLoaded) {
				ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"상태: 하이트맵 로드");
			}
			else {
				ImGui::TextColored(ImVec4(1, 0, 0, 1), u8"상태: 데이터 없음");
			}
			

			ImGui::EndTabItem();
		}

		// 조명 탭
		if (ImGui::BeginTabItem(u8" 조명 & 시간 "))
		{
			ImGui::Text(u8" 시간대 선택 (낮과 밤)");

			// 라이도 버튼으로 시간대 선택
			ImGui::RadioButton(u8"낮 (Day)", &mTimeOfDay, DAY); ImGui::SameLine();
			ImGui::RadioButton(u8"노을 (Sunset)", &mTimeOfDay, SUNSET); ImGui::SameLine();
			ImGui::RadioButton(u8"밤 (Night)", &mTimeOfDay, NIGHT);

			ImGui::Separator();

			ImGui::EndTabItem();
		}

		// 울타리 탭
		if (ImGui::BeginTabItem(u8" 울타리 "))
		{
			ImGui::Text(u8" Fence 세팅 ");

			bool isChanged = false; // 값이 변경되었을 때만 계산하도록
			isChanged |= ImGui::SliderFloat3(u8"위치", mFenceMove, -50.0f, 50.0f);
			isChanged |= ImGui::SliderFloat3(u8"크기", mFenceScale, 0.1f, 50.0f);

			ImGui::Separator();

			if (isChanged && mFenceRitem != nullptr)
			{
				XMMATRIX scale = XMMatrixScaling(mFenceScale[0], mFenceScale[1], mFenceScale[2]);
				XMMATRIX offset = XMMatrixTranslation(
					mFenceBasePos.x + mFenceMove[0],
					mFenceBasePos.y + mFenceMove[1],
					mFenceBasePos.z + mFenceMove[2]
				);

				XMStoreFloat4x4(&mFenceRitem->World, scale * offset);

				mFenceRitem->NumFramesDirty = gNumFrameResources;
			}

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar(); // 탭바 끝
	}

	ImGui::End();
}

void TexWavesApp::UpdateLandVB(const GameTimer& gt)
{
	// 업데이트 필요없으면 리턴
	if (mLandDirtyFrames <= 0) return;

	auto currLandVB = mCurrFrameResource->LandVB.get();
	for (size_t i = 0; i < mLandVertices.size(); ++i)
	{
		Vertex v = mLandVertices[i]; // 원본 정점 데이터 복사
		XMFLOAT3 p = v.Pos;

		if (mUseHeightmap && mIsHeightMapLoaded) {
			p.y = mHeightMap[i] * 10.0f;
			v.Pos = p;

			// 그림자(법선) 계산: 인덱스(i)를 직접 활용
			int W = mHeightMapWidth; // 가로 길이 (보통 256 또는 257)
			// 내 1차원 인덱스(i)를 2차원 (x, y) 좌표로 변환
			int x = i % W;
			int y = i / W;
			
			// 상하좌우 인덱스 구하기 (지형 끝부분에서는 밖으로 안 나가게 내 위치 유지)
			int left = (x > 0) ? (i - 1) : i;
			int right = (x < W - 1) ? (i + 1) : i;
			int top = (y > 0) ? (i - W) : i;
			int bottom = (y < mHeightMapHeight - 1) ? (i + W) : i;

			// 상하좌우의 실제 높이 가져오기
			float hL = mHeightMap[left] * mHeightScale * 10.0f;
			float hR = mHeightMap[right] * mHeightScale * 10.0f;
			float hT = mHeightMap[top] * mHeightScale * 10.0f;
			float hB = mHeightMap[bottom] * mHeightScale * 10.0f;

			// 가운데 2.0f 숫자를 1.0f로 낮추면 그림자가 더 날카로워지고, 5.0f로 높이면 둥글둥글해짐
			XMFLOAT3 n(hL - hR, 2.0f, hB - hT);

			XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
			XMStoreFloat3(&v.Normal, unitNormal);
		}
		else {
			// 하이트맵이 없을 때는 기존 수학 공식 사용
			p.y = mHeightScale * 0.3f * (p.z * sinf(0.1f * p.x) + p.x * cosf(0.1f * p.z));
			v.Pos = p;

			XMFLOAT3 n(
				-mHeightScale * 0.03f * p.z * cosf(0.1f * p.x) - mHeightScale * 0.3f * cosf(0.1f * p.z),
				1.0f,
				-mHeightScale * 0.3f * sinf(0.1f * p.x) + mHeightScale * 0.03f * p.x * sinf(0.1f * p.z)
			);

			XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
			XMStoreFloat3(&v.Normal, unitNormal);
		}		

		// 고도별 색상적용
		if (p.y < mLowThreshold)
		{
			// 아래
			v.Color = mLowColor;
		}
		else if (p.y < mHighThreshold)
		{
			// 중간
			v.Color = mMidColor;
		}
		else
		{
			// 위
			v.Color = mHighColor;
		}

		// 현재 프레임 리소스의 UploadBuffer에 변경된 정점 덮어쓰기
		currLandVB->CopyData(i, v);
	}

	// 카운터 감소 (3 -> 2 -> 1 -> 0)
	mLandDirtyFrames--;
}

void TexWavesApp::LoadHeightmap(std::string filename)
{
	// 파일 읽기 (8비트 기준)
	std::vector<unsigned char> in(mHeightMapWidth * mHeightMapHeight);
	std::ifstream fin;
	fin.open(filename, std::ios_base::binary);

	if (fin)
	{
		fin.read((char*)&in[0], (std::streamsize)in.size());
		fin.close();
	}
	else return;

	// 0~255 사이의 값을 float 높이값으로 변환하여 저장
	mHeightMap.resize(mHeightMapWidth * mHeightMapHeight);
	for (int i = 0; i < mHeightMapWidth * mHeightMapHeight; ++i)
	{
		// 0~255를 0.0~1.0으로 정규화한 뒤, 전체 높이 배율을 곱함
		mHeightMap[i] = (in[i] / 255.0f);
	}

	mIsHeightMapLoaded = true;
	mLandDirtyFrames = gNumFrameResources; // 지형 갱신 신호
}

void TexWavesApp::LoadTextures()
{
	auto grassTex = std::make_unique<Texture>();
	grassTex->Name = "grassTex";
	grassTex->Filename = L"../../Textures/grass.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), grassTex->Filename.c_str(),
		grassTex->Resource, grassTex->UploadHeap));

	auto waterTex = std::make_unique<Texture>();
	waterTex->Name = "waterTex";
	waterTex->Filename = L"../../Textures/water1.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), waterTex->Filename.c_str(),
		waterTex->Resource, waterTex->UploadHeap));

	auto fenceTex = std::make_unique<Texture>();
	fenceTex->Name = "fenceTex";
	fenceTex->Filename = L"../../Textures/WireFence.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), fenceTex->Filename.c_str(),
		fenceTex->Resource, fenceTex->UploadHeap));

	mTextures[grassTex->Name] = std::move(grassTex);
	mTextures[waterTex->Name] = std::move(waterTex);
	mTextures[fenceTex->Name] = std::move(fenceTex);
}

void TexWavesApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void TexWavesApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	UINT texCount = (UINT)mTextures.size(); // 정적할당이 너무 불편해서 동적할당으로 변경함

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = texCount + 1; // +1은 ImGUi용도
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto grassTex = mTextures["grassTex"]->Resource;
	auto waterTex = mTextures["waterTex"]->Resource;
	auto fenceTex = mTextures["fenceTex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = grassTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = waterTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = fenceTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(fenceTex.Get(), &srvDesc, hDescriptor);
}

const D3D_SHADER_MACRO alphaTestDefines[] =
{
	"FOG", "1",
	"ALPHA_TEST", "1",
	NULL, NULL
};

void TexWavesApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_0");
	mShaders["alphaTestPS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", alphaTestDefines, "PS", "ps_5_0");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void TexWavesApp::BuildLandGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);

	//
	// Extract the vertex elements we are interested and apply the height function to
	// each vertex.  In addition, color the vertices based on their height so we have
	// sandy looking beaches, grassy low hills, and snow mountain peaks.
	//

	mLandVertexCount = (UINT)grid.Vertices.size();

	mLandVertices.resize(mLandVertexCount);
	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		auto& p = grid.Vertices[i].Position;
		mLandVertices[i].Pos = p;
		mLandVertices[i].Pos.y = GetHillsHeight(p.x, p.z);
		mLandVertices[i].Normal = GetHillsNormal(p.x, p.z);
		mLandVertices[i].TexC = grid.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)mLandVertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = grid.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), mLandVertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), mLandVertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["landGeo"] = std::move(geo);
}

void TexWavesApp::BuildWavesGeometry()
{
	std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount()); // 3 indices per face
	assert(mWaves->VertexCount() < 0x0000ffff);

	// Iterate over each quad.
	int m = mWaves->RowCount();
	int n = mWaves->ColumnCount();
	int k = 0;
	for (int i = 0; i < m - 1; ++i)
	{
		for (int j = 0; j < n - 1; ++j)
		{
			indices[k] = i * n + j;
			indices[k + 1] = i * n + j + 1;
			indices[k + 2] = (i + 1) * n + j;

			indices[k + 3] = (i + 1) * n + j;
			indices[k + 4] = i * n + j + 1;
			indices[k + 5] = (i + 1) * n + j + 1;

			k += 6; // next quad
		}
	}

	UINT vbByteSize = mWaves->VertexCount() * sizeof(Vertex);
	UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "waterGeo";

	// Set dynamically.
	geo->VertexBufferCPU = nullptr;
	geo->VertexBufferGPU = nullptr;

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["waterGeo"] = std::move(geo);
}

void TexWavesApp::BuildBoxGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(8.0f, 8.0f, 8.0f, 3);

	std::vector<Vertex> vertices(box.Vertices.size());
	for (size_t i = 0; i < box.Vertices.size(); ++i)
	{
		auto& p = box.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TexC = box.Vertices[i].TexC;
		vertices[i].Color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = box.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "boxGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["box"] = submesh;

	mGeometries["boxGeo"] = std::move(geo);
}

void TexWavesApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));


	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

	// 블렌딩 설정 투명도
	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

	// 울타리용
	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestPsoDesc = opaquePsoDesc;

	alphaTestPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["alphaTestPS"]->GetBufferPointer()),
		mShaders["alphaTestPS"]->GetBufferSize()
	};
	// 백페이스 컬링 끄기
	alphaTestPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaTestPsoDesc, IID_PPV_ARGS(&mPSOs["alphaTest"])));
}

void TexWavesApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), mWaves->VertexCount(), mLandVertexCount));
	}
}

void TexWavesApp::BuildMaterials()
{
	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = 0;
	grass->DiffuseSrvHeapIndex = 0;
	grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.125f;

	// This is not a good water material definition, but we do not have all the rendering
	// tools we need (transparency, environment reflection), so we fake it for now.
	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = 1;
	water->DiffuseSrvHeapIndex = 1;
	water->DiffuseAlbedo = XMFLOAT4(0.0f, 0.2f, 0.6f, 0.5f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;

	auto wirefence = std::make_unique<Material>();
	wirefence->Name = "wirefence";
	wirefence->MatCBIndex = 2;
	wirefence->DiffuseSrvHeapIndex = 2;
	wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	wirefence->Roughness = 0.25f;

	mMaterials["grass"] = std::move(grass);
	mMaterials["water"] = std::move(water);
	mMaterials["wirefence"] = std::move(wirefence);
}

void TexWavesApp::BuildRenderItems()
{
	auto wavesRitem = std::make_unique<RenderItem>();
	XMMATRIX scale = XMMatrixScaling(5.0f, 1.0f, 5.0f);
	XMMATRIX offset = XMMatrixTranslation(0.0f, 0.5f, 0.0f);
	XMStoreFloat4x4(&wavesRitem->World, XMMatrixMultiply(scale, offset));
	XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(25.0f, 25.0f, 1.0f));
	wavesRitem->ObjCBIndex = 0;
	wavesRitem->Mat = mMaterials["water"].get();
	wavesRitem->Geo = mGeometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mWavesRitem = wavesRitem.get();

	mRitemLayer[(int)RenderLayer::Transparent].push_back(wavesRitem.get());

	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->World, XMMatrixScaling(5.0f, 1.0f, 5.0f));
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(15.0f, 15.0f, 1.0f));
	gridRitem->ObjCBIndex = 1;
	gridRitem->Mat = mMaterials["grass"].get();
	gridRitem->Geo = mGeometries["landGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());

	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(20.0f, 20.0f, 0.1f) * XMMatrixTranslation(0.0f, 2.5f, 0.0f));
	boxRitem->ObjCBIndex = 2;
	boxRitem->Mat = mMaterials["wirefence"].get();
	boxRitem->Geo = mGeometries["boxGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mFenceRitem = boxRitem.get();

	mRitemLayer[(int)RenderLayer::AlphaTest].push_back(boxRitem.get()); //일단 화면에 안그림

	mAllRitems.push_back(std::move(wavesRitem));
	mAllRitems.push_back(std::move(gridRitem));
	mAllRitems.push_back(std::move(boxRitem));
}

void TexWavesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		if (ri->Geo->Name == "landGeo")
		{
			auto landVBView = mCurrFrameResource->LandVB->Resource()->GetGPUVirtualAddress();
			D3D12_VERTEX_BUFFER_VIEW vbv;
			vbv.BufferLocation = landVBView;
			vbv.StrideInBytes = sizeof(Vertex);
			vbv.SizeInBytes = mLandVertexCount * sizeof(Vertex); // 총 바이트 크기

			cmdList->IASetVertexBuffers(0, 1, &vbv);
		}
		else if (ri->Geo->Name == "waterGeo")
		{
			auto wavesVBView = mCurrFrameResource->WavesVB->Resource()->GetGPUVirtualAddress();
			D3D12_VERTEX_BUFFER_VIEW vbv;
			vbv.BufferLocation = wavesVBView;
			vbv.StrideInBytes = sizeof(Vertex);
			vbv.SizeInBytes = mWaves->VertexCount() * sizeof(Vertex);

			cmdList->IASetVertexBuffers(0, 1, &vbv);
		}
		else
		{
			// 상자 등 나머지 움직이지 않는 물체들은 원래 있던 정적 버퍼 사용
			cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		}

		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> TexWavesApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}

float TexWavesApp::GetHillsHeight(float x, float z)const
{
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 TexWavesApp::GetHillsNormal(float x, float z)const
{
	// n = (-df/dx, 1, -df/dz)
	XMFLOAT3 n(
		-0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
		1.0f,
		-0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, unitNormal);

	return n;
}
