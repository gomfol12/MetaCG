/**
 * File: StatementCountCollector.h
 * License: Part of the MetaCG project. Licensed under BSD 3 clause license. See LICENSE.txt file at
 * https://github.com/tudasc/metacg/LICENSE.txt
 */

#ifndef STATEMENTCOUNTCOLLECTOR_H
#define STATEMENTCOUNTCOLLECTOR_H

#include "FortranUtil.h"

#include "metacg/Callgraph.h"
#include "metacg/io/IdMapping.h"
#include <cstddef>
#include <flang/Parser/parse-tree.h>
#include <optional>

class StatementCountMetadata : public metacg::MetaData::Registrar<StatementCountMetadata> {
 public:
  static constexpr const char* key = "statement_count";

  StatementCountMetadata(std::size_t count) : count(count) {}

  explicit StatementCountMetadata(const nlohmann::json& j, metacg::StrToNodeMapping&) {
    metacg::MCGLogger::instance().getConsole()->trace("Reading StatementCountMetadata from json");
    if (j.is_null()) {
      metacg::MCGLogger::instance().getConsole()->trace("Could not retrieve meta data for {}",
                                                        "StatementCountMetadata");
      return;
    }
    count = j.get<std::size_t>();
    metacg::MCGLogger::instance().getConsole()->debug("Read {} statements from file", count);
  };

  nlohmann::json toJson(metacg::NodeToStrMapping&) const final { return count; }

  const char* getKey() const override { return key; }

  void merge(const MetaData& toMerge, std::optional<metacg::MergeAction>, const metacg::GraphMapping&) final {
    if (std::strcmp(toMerge.getKey(), getKey()) != 0) {
      metacg::MCGLogger::instance().getErrConsole()->error(
          "The MetaData which was tried to merge with StatementCountMetadata was of a different MetaData type");
      abort();
    }
    const auto* toMergeDerived = static_cast<const StatementCountMetadata*>(&toMerge);
    count += toMergeDerived->getCount();
  }

  std::unique_ptr<MetaData> clone() const final { return std::unique_ptr<MetaData>(new StatementCountMetadata(*this)); }

  void applyMapping(const metacg::GraphMapping&) override {}

  void setCount(std::size_t c) { count = c; }
  std::size_t getCount() const { return count; }

 private:
  std::size_t count{0};
};

class StatementCountVisitor {
 public:
  StatementCountVisitor(metacg::Callgraph* cg, bool underscoring) : cg(cg), underscoring(underscoring) {}

  template <typename A>
  bool Pre(const A&) {
    return true;
  }
  template <typename A>
  void Post(const A&) {}

  bool Pre(const Fortran::parser::MainProgram& p) {
    inProcedure = true;
    statementCounts.push_back(0);
    return true;
  }
  bool Pre(const Fortran::parser::FunctionSubprogram&) {
    inProcedure = true;
    statementCounts.push_back(0);
    return true;
  }
  bool Pre(const Fortran::parser::SubroutineSubprogram&) {
    inProcedure = true;
    statementCounts.push_back(0);
    return true;
  }
  void Post(const Fortran::parser::MainProgram& p) {
    auto& stmt = std::get<std::optional<Fortran::parser::Statement<Fortran::parser::ProgramStmt>>>(p.t);
    if (!stmt.has_value()) {
      return;
    }
    auto symbol = stmt.value().statement.v.symbol;
    auto mangledName = metacg::cgfcollector::mangleSymbol(symbol, underscoring);
    addStatementCountToNode(mangledName);

    inProcedure = false;
    statementCounts.pop_back();
  }
  void Post(const Fortran::parser::FunctionSubprogram& p) {
    auto& stmt = std::get<Fortran::parser::Statement<Fortran::parser::FunctionStmt>>(p.t);
    auto* symbol = std::get<Fortran::parser::Name>(stmt.statement.t).symbol;
    auto mangledName = metacg::cgfcollector::mangleSymbol(symbol, underscoring);
    addStatementCountToNode(mangledName);

    inProcedure = false;
    statementCounts.pop_back();
  }
  void Post(const Fortran::parser::SubroutineSubprogram& p) {
    auto& stmt = std::get<Fortran::parser::Statement<Fortran::parser::SubroutineStmt>>(p.t);
    auto* symbol = std::get<Fortran::parser::Name>(stmt.statement.t).symbol;
    auto mangledName = metacg::cgfcollector::mangleSymbol(symbol, underscoring);
    addStatementCountToNode(mangledName);

    inProcedure = false;
    statementCounts.pop_back();
  }

  void addStatementCountToNode(const std::string& mangledName) {
    auto* node = cg->getFirstNode(mangledName);
    if (!node) {
      return;
    }
    node->addMetaData(std::move(std::make_unique<StatementCountMetadata>(statementCounts.back())));
  }

  void incrementCount() {
    if (inProcedure) {
      statementCounts.back()++;
    }
  }

  // Count statements in the procedure body

#define COUNT_STATEMENT(stmtType)                                                                                      \
  bool Pre(const Fortran::parser::stmtType&) {                                                                         \
    incrementCount();                                                                                                  \
    return true;                                                                                                       \
  }

  COUNT_STATEMENT(ImplicitStmt)
  COUNT_STATEMENT(DeclarationConstruct)
  COUNT_STATEMENT(ExecutionPartConstruct)

  COUNT_STATEMENT(WhereBodyConstruct)
  COUNT_STATEMENT(ForallBodyConstruct)

 private:
  bool inProcedure{false};
  bool underscoring;
  metacg::Callgraph* cg;
  std::vector<std::size_t> statementCounts;
};

#endif  // STATEMENTCOUNTCOLLECTOR_H
