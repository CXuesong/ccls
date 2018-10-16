/* Copyright 2017-2018 ccls Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "message_handler.h"
#include "pipeline.hh"
#include "query_utils.h"
using namespace ccls;

namespace {
MethodType kMethodType = "textDocument/hover";

const char *LanguageIdentifier(LanguageId lang) {
  switch (lang) {
  // clang-format off
  case LanguageId::C: return "c";
  case LanguageId::Cpp: return "cpp";
  case LanguageId::ObjC: return "objective-c";
  case LanguageId::ObjCpp: return "objective-cpp";
  default: return "";
  // clang-format on
  }
}

// Returns the hover or detailed name for `sym`, if any.
std::pair<std::optional<lsMarkedString>, std::optional<lsMarkedString>>
GetHover(DB *db, LanguageId lang, SymbolRef sym, int file_id) {
  const char *comments = nullptr;
  std::optional<lsMarkedString> ls_comments, hover;
  WithEntity(db, sym, [&](const auto &entity) {
    std::remove_reference_t<decltype(entity.def[0])> *def = nullptr;
    for (auto &d : entity.def) {
      if (d.spell) {
        comments = d.comments[0] ? d.comments : nullptr;
        def = &d;
        if (d.spell->file_id == file_id)
          break;
      }
    }
    if (!def && entity.def.size()) {
      def = &entity.def[0];
      if (def->comments[0])
        comments = def->comments;
    }
    if (def) {
      lsMarkedString m;
      m.language = LanguageIdentifier(lang);
      if (def->hover[0]) {
        m.value = def->hover;
        hover = m;
      } else if (def->detailed_name[0]) {
        m.value = def->detailed_name;
        hover = m;
      }
      if (comments)
        ls_comments = lsMarkedString{std::nullopt, comments};
    }
  });
  return {hover, ls_comments};
}

struct In_TextDocumentHover : public RequestMessage {
  MethodType GetMethodType() const override { return kMethodType; }
  lsTextDocumentPositionParams params;
};
MAKE_REFLECT_STRUCT(In_TextDocumentHover, id, params);
REGISTER_IN_MESSAGE(In_TextDocumentHover);

struct lsHover {
  std::vector<lsMarkedString> contents;
  std::optional<lsRange> range;
};
MAKE_REFLECT_STRUCT(lsHover, contents, range);

struct Handler_TextDocumentHover : BaseMessageHandler<In_TextDocumentHover> {
  MethodType GetMethodType() const override { return kMethodType; }
  void Run(In_TextDocumentHover *request) override {
    auto &params = request->params;
    QueryFile *file;
    if (!FindFileOrFail(db, project, request->id,
                        params.textDocument.uri.GetPath(), &file))
      return;

    WorkingFile *wfile = working_files->GetFileByFilename(file->def->path);
    lsHover result;

    for (SymbolRef sym : FindSymbolsAtLocation(wfile, file, params.position)) {
      std::optional<lsRange> ls_range = GetLsRange(
          working_files->GetFileByFilename(file->def->path), sym.range);
      if (!ls_range)
        continue;

      auto [hover, comments] = GetHover(db, file->def->language, sym, file->id);
      if (comments || hover) {
        result.range = *ls_range;
        if (comments)
          result.contents.push_back(*comments);
        if (hover)
          result.contents.push_back(*hover);
        break;
      }
    }

    pipeline::Reply(request->id, result);
  }
};
REGISTER_MESSAGE_HANDLER(Handler_TextDocumentHover);
} // namespace
