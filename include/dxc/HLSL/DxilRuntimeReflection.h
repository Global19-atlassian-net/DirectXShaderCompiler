///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// DxilLibraryReflection.h                                                   //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
//                                                                           //
// Defines shader reflection for runtime usage.                              //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include <unordered_map>
#include <vector>
#include <memory>
#include "DxilConstants.h"

namespace hlsl {
namespace DXIL {
namespace RDAT {

struct RuntimeDataTableHeader {
  uint32_t tableType; // RuntimeDataPartType
  uint32_t size;
  uint32_t offset;
};

enum RuntimeDataPartType : uint32_t {
  Invalid = 0,
  String,
  Function,
  Resource,
  Index
};

// Index table is a sequence of rows, where each row has a count as a first
// element followed by the count number of elements pre computing values
class IndexTableReader {
private:
  const uint32_t *m_table;
  uint32_t m_size;

public:
  class IndexRow {
  private:
    const uint32_t *m_values;
    const uint32_t m_count;

  public:
    IndexRow(const uint32_t *values, uint32_t count)
        : m_values(values), m_count(count) {}
    uint32_t Count() { return m_count; }
    uint32_t At(uint32_t i) { return m_values[i]; }
  };

  IndexTableReader() : m_table(nullptr), m_size(0) {}
  IndexTableReader(const uint32_t *table, uint32_t size)
      : m_table(table), m_size(size) {}

  void SetTable(const uint32_t *table) { m_table = table; }

  void SetSize(uint32_t size) { m_size = size; }

  IndexRow getRow(uint32_t i) { return IndexRow(&m_table[i] + 1, m_table[i]); }
};

class StringTableReader {
  const char *m_table;
  uint32_t m_size;
public:
  StringTableReader() : m_table(nullptr), m_size(0) {}
  StringTableReader(const char *table, uint32_t size)
      : m_table(table), m_size(size) {}
  const char *Get(uint32_t offset) const {
    _Analysis_assume_(offset < m_size && m_table &&
                      m_table[m_size - 1] == '\0');
    return m_table + offset;
  }
};

struct RuntimeDataResourceInfo {
  uint32_t Class; // hlsl::DXIL::ResourceClass
  uint32_t Kind;  // hlsl::DXIL::ResourceKind
  uint32_t ID;    // id per class
  uint32_t Space;
  uint32_t LowerBound;
  uint32_t UpperBound;
  uint32_t Name;  // resource name as an offset for string table
  uint32_t Flags; // Not implemented yet
};

struct RuntimeDataFunctionInfo {
  uint32_t Name;                 // offset for string table
  uint32_t UnmangledName;        // offset for string table
  uint32_t Resources;            // index to an index table
  uint32_t FunctionDependencies; // index to a list of functions that function
                                 // depends on
  uint32_t ShaderKind;
  uint32_t PayloadSizeInBytes;   // 1) hit, miss, or closest shader: payload count
                                 // 2) call shader: parameter size 
  uint32_t AttributeSizeInBytes; // attribute size for closest hit and any hit
  uint32_t FeatureInfo1;         // first 32 bits of feature flag
  uint32_t FeatureInfo2;         // second 32 bits of feature flag
  uint32_t ShaderStageFlag;      // valid shader stage flag. Not implemented yet.
  uint32_t MinShaderTarget;      // minimum shader target. Not implemented yet.
};

class ResourceTableReader;
class FunctionTableReader;

struct RuntimeDataContext {
  StringTableReader *pStringTableReader;
  IndexTableReader *pIndexTableReader;
  ResourceTableReader *pResourceTableReader;
  FunctionTableReader *pFunctionTableReader;
};

class ResourceReader {
private:
  const RuntimeDataResourceInfo *m_ResourceInfo;
  RuntimeDataContext *m_Context;

public:
  ResourceReader(const RuntimeDataResourceInfo *resInfo,
                 RuntimeDataContext *context)
      : m_ResourceInfo(resInfo), m_Context(context) {}
  hlsl::DXIL::ResourceClass GetResourceClass() const {
    return (hlsl::DXIL::ResourceClass)m_ResourceInfo->Class;
  }
  uint32_t GetSpace() const { return m_ResourceInfo->Space; }
  uint32_t GetLowerBound() const { return m_ResourceInfo->LowerBound; }
  uint32_t GetUpperBound() const { return m_ResourceInfo->UpperBound; }
  hlsl::DXIL::ResourceKind GetResourceKind() const {
    return (hlsl::DXIL::ResourceKind)m_ResourceInfo->Kind;
  }
  uint32_t GetID() const { return m_ResourceInfo->ID; }
  const char *GetName() const {
    return m_Context->pStringTableReader->Get(m_ResourceInfo->Name);
  }
  uint32_t GetFlags() const { return m_ResourceInfo->Flags; }
};

class ResourceTableReader {
private:
  const RuntimeDataResourceInfo
      *m_ResourceInfo; // pointer to an array of resource bind infos
  RuntimeDataContext *m_Context;
  uint32_t m_CBufferCount;
  uint32_t m_SamplerCount;
  uint32_t m_SRVCount;
  uint32_t m_UAVCount;

public:
  ResourceTableReader()
      : m_ResourceInfo(nullptr), m_Context(nullptr), m_CBufferCount(0),
        m_SamplerCount(0), m_SRVCount(0), m_UAVCount(0){};
  ResourceTableReader(const RuntimeDataResourceInfo *info1,
                      RuntimeDataContext *context, uint32_t CBufferCount,
                      uint32_t SamplerCount, uint32_t SRVCount,
                      uint32_t UAVCount)
      : m_ResourceInfo(info1), m_Context(context), m_CBufferCount(CBufferCount),
        m_SamplerCount(SamplerCount), m_SRVCount(SRVCount),
        m_UAVCount(UAVCount){};

  void SetResourceInfo(const RuntimeDataResourceInfo *ptr, uint32_t count) {
    m_ResourceInfo = ptr;
    // Assuming that resources are in order of CBuffer, Sampler, SRV, and UAV,
    // count the number for each resource class
    m_CBufferCount = 0;
    m_SamplerCount = 0;
    m_SRVCount = 0;
    m_UAVCount = 0;

    for (uint32_t i = 0; i < count; ++i) {
      const RuntimeDataResourceInfo *curPtr = &ptr[i];
      if (curPtr->Class == (uint32_t)hlsl::DXIL::ResourceClass::CBuffer)
        m_CBufferCount++;
      else if (curPtr->Class == (uint32_t)hlsl::DXIL::ResourceClass::Sampler)
        m_SamplerCount++;
      else if (curPtr->Class == (uint32_t)hlsl::DXIL::ResourceClass::SRV)
        m_SRVCount++;
      else if (curPtr->Class == (uint32_t)hlsl::DXIL::ResourceClass::UAV)
        m_UAVCount++;
    }
  }

  void SetContext(RuntimeDataContext *context) { m_Context = context; }

  uint32_t GetNumResources() const {
    return m_CBufferCount + m_SamplerCount + m_SRVCount + m_UAVCount;
  }
  ResourceReader GetItem(uint32_t i) const {
    _Analysis_assume_(i < GetNumResources());
    return ResourceReader(&m_ResourceInfo[i], m_Context);
  }

  uint32_t GetNumCBuffers() const { return m_CBufferCount; }
  ResourceReader GetCBuffer(uint32_t i) {
    _Analysis_assume_(i < m_CBufferCount);
    return ResourceReader(&m_ResourceInfo[i], m_Context);
  }

  uint32_t GetNumSamplers() const { return m_SamplerCount; }
  ResourceReader GetSampler(uint32_t i) {
    _Analysis_assume_(i < m_SamplerCount);
    uint32_t offset = (m_CBufferCount + i);
    return ResourceReader(&m_ResourceInfo[offset], m_Context);
  }

  uint32_t GetNumSRVs() const { return m_SRVCount; }
  ResourceReader GetSRV(uint32_t i) {
    _Analysis_assume_(i < m_SRVCount);
    uint32_t offset = (m_CBufferCount + m_SamplerCount + i);
    return ResourceReader(&m_ResourceInfo[offset], m_Context);
  }

  uint32_t GetNumUAVs() const { return m_UAVCount; }
  ResourceReader GetUAV(uint32_t i) {
    _Analysis_assume_(i < m_UAVCount);
    uint32_t offset = (m_CBufferCount + m_SamplerCount + m_SRVCount + i);
    return ResourceReader(&m_ResourceInfo[offset], m_Context);
  }
};

class FunctionReader {
private:
  const RuntimeDataFunctionInfo *m_RuntimeDataFunctionInfo;
  RuntimeDataContext *m_Context;

public:
  FunctionReader() : m_RuntimeDataFunctionInfo(nullptr), m_Context(nullptr) {}
  FunctionReader(const RuntimeDataFunctionInfo *functionInfo,
                 RuntimeDataContext *context)
      : m_RuntimeDataFunctionInfo(functionInfo), m_Context(context) {}

  const char *GetName() const {
    return m_Context->pStringTableReader->Get(m_RuntimeDataFunctionInfo->Name);
  }
  const char *GetUnmangledName() const {
    return m_Context->pStringTableReader->Get(
        m_RuntimeDataFunctionInfo->UnmangledName);
  }
  uint64_t GetFeatureFlag() const {
    uint64_t flag =
        static_cast<uint64_t>(m_RuntimeDataFunctionInfo->FeatureInfo2) << 32;
    flag |= static_cast<uint64_t>(m_RuntimeDataFunctionInfo->FeatureInfo1);
    return flag;
  }
  uint32_t GetFeatureInfo1() const {
    return m_RuntimeDataFunctionInfo->FeatureInfo1;
  }
  uint32_t GetFeatureInfo2() const {
    return m_RuntimeDataFunctionInfo->FeatureInfo2;
  }

  uint32_t GetShaderStageFlag() const {
    return m_RuntimeDataFunctionInfo->ShaderStageFlag;
  }
  uint32_t GetMinShaderTarget() const {
    return m_RuntimeDataFunctionInfo->MinShaderTarget;
  }
  uint32_t GetNumResources() const {
    if (m_RuntimeDataFunctionInfo->Resources == UINT_MAX)
      return 0;
    return m_Context->pIndexTableReader
      ->getRow(m_RuntimeDataFunctionInfo->Resources)
      .Count();
  }
  ResourceReader GetResource(uint32_t i) const {
    uint32_t resIndex = m_Context->pIndexTableReader
      ->getRow(m_RuntimeDataFunctionInfo->Resources)
      .At(i);
    return m_Context->pResourceTableReader->GetItem(resIndex);
  }
  uint32_t GetNumDependencies() const {
    if (m_RuntimeDataFunctionInfo->FunctionDependencies == UINT_MAX)
      return 0;
    return m_Context->pIndexTableReader
      ->getRow(m_RuntimeDataFunctionInfo->FunctionDependencies)
      .Count();
  }
  const char *GetDependency(uint32_t i) const {
    uint32_t resIndex =
      m_Context->pIndexTableReader
      ->getRow(m_RuntimeDataFunctionInfo->FunctionDependencies)
      .At(i);
    return m_Context->pStringTableReader->Get(resIndex);
  }

  uint32_t GetPayloadSizeInBytes() const {
    return m_RuntimeDataFunctionInfo->PayloadSizeInBytes;
  }
  uint32_t GetAttributeSizeInBytes() const {
    return m_RuntimeDataFunctionInfo->AttributeSizeInBytes;
  }
  // payload (hit shaders) and parameters (call shaders) are mutually exclusive
  uint32_t GetParameterSizeInBytes() const {
    return m_RuntimeDataFunctionInfo->PayloadSizeInBytes;
  }
  hlsl::DXIL::ShaderKind GetShaderKind() const {
    return (hlsl::DXIL::ShaderKind)m_RuntimeDataFunctionInfo->ShaderKind;
  }
};

class FunctionTableReader {
private:
  const RuntimeDataFunctionInfo *m_infos;
  uint32_t m_count;
  RuntimeDataContext *m_context;

public:
  FunctionTableReader() : m_infos(nullptr), m_count(0), m_context(nullptr) {}
  FunctionTableReader(const RuntimeDataFunctionInfo *functionInfos,
                      uint32_t count, RuntimeDataContext *context)
      : m_infos(functionInfos), m_count(count), m_context(context) {}

  FunctionReader GetItem(uint32_t i) const {
    return FunctionReader(&m_infos[i], m_context);
  }
  uint32_t GetNumFunctions() const { return m_count; }

  void SetFunctionInfo(const RuntimeDataFunctionInfo *ptr) {
    m_infos = ptr;
  }
  void SetCount(uint32_t count) { m_count = count; }
  void SetContext(RuntimeDataContext *context) { m_context = context; }
};

class DxilRuntimeData {
private:
  uint32_t m_TableCount;
  StringTableReader m_StringReader;
  IndexTableReader m_IndexTableReader;
  ResourceTableReader m_ResourceTableReader;
  FunctionTableReader m_FunctionTableReader;
  RuntimeDataContext m_Context;

public:
  DxilRuntimeData();
  DxilRuntimeData(const char *ptr);
  // initializing reader from RDAT. return true if no error has occured.
  bool InitFromRDAT(const void *pRDAT);
  FunctionTableReader *GetFunctionTableReader();
  ResourceTableReader *GetResourceTableReader();
};

//////////////////////////////////
/// structures for library runtime

typedef struct DXIL_RESOURCE {
  uint32_t Class; // hlsl::DXIL::ResourceClass
  uint32_t Kind;  // hlsl::DXIL::ResourceKind
  uint32_t ID;    // id per class
  uint32_t Space;
  uint32_t UpperBound;
  uint32_t LowerBound;
  LPCWSTR Name;
  uint32_t Flags;
} DXIL_RESOURCE;

typedef struct DXIL_FUNCTION {
  LPCWSTR Name;
  LPCWSTR UnmangledName;
  uint32_t NumResources;
  const DXIL_RESOURCE *Resources;
  uint32_t NumFunctionDependencies;
  const LPCWSTR *FunctionDependencies;
  uint32_t ShaderKind;
  uint32_t PayloadSizeInBytes;   // 1) hit, miss, or closest shader: payload count
                                 // 2) call shader: parameter size
  uint32_t AttributeSizeInBytes; // attribute size for closest hit and any hit
  uint32_t FeatureInfo1;         // first 32 bits of feature flag
  uint32_t FeatureInfo2;         // second 32 bits of feature flag
  uint32_t ShaderStageFlag;      // valid shader stage flag. Not implemented yet.
  uint32_t MinShaderTarget;      // minimum shader target. Not implemented yet.
} DXIL_FUNCITON;

typedef struct DXIL_SUBOBJECT {
} DXIL_SUBOBJECT;

typedef struct DXIL_LIBRARY_DESC {
  uint32_t NumFunctions;
  DXIL_FUNCITON *pFunction;
  uint32_t NumResources;
  DXIL_RESOURCE *pResource;
  uint32_t NumSubobjects;
  DXIL_SUBOBJECT *pSubobjects;
} DXIL_LIBRARY_DESC;

class DxilRuntimeReflection {
private:
  typedef std::unordered_map<const char *, std::unique_ptr<wchar_t[]>> StringMap;
  typedef std::vector<DXIL_RESOURCE> ResourceList;
  typedef std::vector<DXIL_RESOURCE *> ResourceRefList;
  typedef std::vector<DXIL_FUNCTION> FunctionList;
  typedef std::vector<const wchar_t *> WStringList;

  DxilRuntimeData m_RuntimeData;
  StringMap m_StringMap;
  ResourceList m_Resources;
  FunctionList m_Functions;
  std::unordered_map<DXIL_FUNCTION *, ResourceRefList> m_FuncToResMap;
  std::unordered_map<DXIL_FUNCTION *, WStringList> m_FuncToStringMap;
  bool m_initialized;

  const wchar_t *GetWideString(const char *ptr);
  void AddString(const char *ptr);
  void InitializeReflection();
  DXIL_RESOURCE *GetResourcesForFunction(DXIL_FUNCTION &function,
                                         const FunctionReader &functionReader);
  const wchar_t **GetDependenciesForFunction(DXIL_FUNCTION &function,
                             const FunctionReader &functionReader);
  DXIL_RESOURCE *AddResource(const ResourceReader &resourceReader);
  DXIL_FUNCTION *AddFunction(const FunctionReader &functionReader);

public:
  // TODO: Implement pipeline state validation with runtime data
  // TODO: Update BlobContainer.h to recognize 'RDAT' blob
  DxilRuntimeReflection()
      : m_RuntimeData(), m_StringMap(), m_Resources(), m_Functions(),
        m_FuncToResMap(), m_FuncToStringMap(), m_initialized(false) {}
  // This call will allocate memory for GetLibraryReflection call
  bool InitFromRDAT(const void *pRDAT);
  const DXIL_LIBRARY_DESC GetLibraryReflection();
};

} // namespace LIB
} // namespace DXIL
} // namespace hlsl