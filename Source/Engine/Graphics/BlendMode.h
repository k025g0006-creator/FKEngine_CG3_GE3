#pragma once

#include <d3d12.h>

// ---------------------------------------------
// BlendMode
// ---------------------------------------------
enum BlendMode
{
	kBlendModeNone,      // ブレンドなし
	kBlendModeNormal,    // 通常αブレンド。Src * SrcA + Dest * (1 - SrcA)
	kBlendModeAdd,       // 加算。Src * SrcA + Dest * 1
	kBlendModeSubtract,  // 減算。Dest * 1 - Src * SrcA
	kBlendModeMultiply,  // 乗算。Src * 0 + Dest * Src
	kBlendModeScreen,    // スクリーン。Src * (1 - Dest) + Dest * 1
	kCountOfBlendMode,   // 利用してはいけない
};

inline D3D12_BLEND_DESC CreateBlendDesc(BlendMode mode)
{
	D3D12_BLEND_DESC desc{};
	auto& rt = desc.RenderTarget[0];

	// 全ての色要素を書き込む
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// α値のブレンド設定は全モード共通
	rt.SrcBlendAlpha = D3D12_BLEND_ONE;
	rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rt.DestBlendAlpha = D3D12_BLEND_ZERO;

	if (mode == kBlendModeNone)
	{
		rt.BlendEnable = FALSE;
		return desc;
	}

	rt.BlendEnable = TRUE;

	switch (mode)
	{
	case kBlendModeNormal:
		rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		break;
	case kBlendModeAdd:
		rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.DestBlend = D3D12_BLEND_ONE;
		break;
	case kBlendModeSubtract:
		rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rt.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		rt.DestBlend = D3D12_BLEND_ONE;
		break;
	case kBlendModeMultiply:
		rt.SrcBlend = D3D12_BLEND_ZERO;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.DestBlend = D3D12_BLEND_SRC_COLOR;
		break;
	case kBlendModeScreen:
		rt.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.DestBlend = D3D12_BLEND_ONE;
		break;
	default:
		break;
	}
	return desc;
}

