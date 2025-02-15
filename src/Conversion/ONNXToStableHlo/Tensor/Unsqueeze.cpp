/*
 * SPDX-License-Identifier: Apache-2.0
 */

//===--------------- Unsqueeze.cpp - Lowering Unsqueeze Op ----------------===//
//
// Copyright 2022
//
// =============================================================================
//
// This file lowers the ONNX Unsqueeze Operator to StableHlo dialect.
//
//===----------------------------------------------------------------------===//

#include "src/Conversion/ONNXToStableHlo/DialectBuilder.hpp"
#include "src/Conversion/ONNXToStableHlo/ONNXToStableHloCommon.hpp"
#include "src/Dialect/ONNX/ONNXOps/ShapeHelper.hpp"
#include "src/Support/TypeUtilities.hpp"

using namespace mlir;

namespace onnx_mlir {

namespace {

// ONNXUnsqueezeOp(A) is implemented using StableHlo reshapeOp
struct ONNXUnsqueezeOpLoweringToStableHlo : public ConversionPattern {
  ONNXUnsqueezeOpLoweringToStableHlo(MLIRContext *ctx)
      : ConversionPattern(mlir::ONNXUnsqueezeOp::getOperationName(), 1, ctx) {}

  LogicalResult matchAndRewrite(Operation *op, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const final {
    ONNXUnsqueezeOpAdaptor operandAdaptor(operands);
    ONNXUnsqueezeOp unsqueezeOp = llvm::cast<ONNXUnsqueezeOp>(op);
    Location loc = op->getLoc();
    Value data = unsqueezeOp.getData();
    Value axes = unsqueezeOp.getAxes();
    assert(isRankedShapedType(data.getType()) &&
           "data must be ranked Shaped Type");
    ShapedType dataType = data.getType().cast<ShapedType>();
    int64_t rank = dataType.getRank();

    // Unused; for example, axles can be read from it.
    // Code below does not seems to handle >v11 where axles are inputs.
    IndexExprBuilderForStableHlo createIE(rewriter, loc);
    ONNXUnsqueezeOpShapeHelper shapeHelper(op, operands, &createIE);
    shapeHelper.computeShapeAndAssertOnFailure();

    SmallVector<int64_t, 4> axesList;
    if (ElementsAttr axesAttr = getElementAttributeFromONNXValue(axes)) {
      for (IntegerAttr value : axesAttr.getValues<IntegerAttr>()) {
        int64_t axis = value.cast<IntegerAttr>().getInt();
        if (axis < 0)
          axis += rank;
        axesList.push_back(axis);
      }
    }

    int64_t newRank = rank + axesList.size();
    SmallVector<Value, 4> newShape;
    SmallVector<bool, 4> isUnsqueezeDim(newRank, false);
    Value dataShape = rewriter.create<shape::ShapeOfOp>(loc, data);
    for (int64_t axis : axesList) {
      isUnsqueezeDim[axis] = true;
    }
    for (int64_t i = 0, j = 0; i < newRank; i++) {
      if (isUnsqueezeDim[i]) {
        Value indexValue = rewriter.create<arith::ConstantIndexOp>(loc, 1);
        newShape.push_back(indexValue);
      } else {
        Value dim = rewriter.create<shape::GetExtentOp>(loc, dataShape, j);
        newShape.push_back(dim);
        j++;
      }
    }
    Type outputShapeType =
        RankedTensorType::get({newRank}, rewriter.getIndexType());
    Value newShapeValue = rewriter.create<shape::FromExtentsOp>(loc, newShape);
    newShapeValue = rewriter.create<shape::ToExtentTensorOp>(
        loc, outputShapeType, newShapeValue);
    Type outputType = *op->result_type_begin();
    Value result = rewriter.create<stablehlo::DynamicReshapeOp>(
        loc, outputType, data, newShapeValue);
    rewriter.replaceOp(op, result);
    return success();
  }
};

} // namespace

void populateLoweringONNXUnsqueezeOpToStableHloPattern(
    RewritePatternSet &patterns, MLIRContext *ctx) {
  patterns.insert<ONNXUnsqueezeOpLoweringToStableHlo>(ctx);
}

} // namespace onnx_mlir
