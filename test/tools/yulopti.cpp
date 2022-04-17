/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0
/**
 * Interactive yul optimizer
 */

#include <libsolutil/CommonIO.h>
#include <libsolutil/Exceptions.h>
#include <libsolutil/StringUtils.h>
#include <liblangutil/ErrorReporter.h>
#include <libyul/AsmAnalysis.h>
#include <libyul/AsmAnalysisInfo.h>
#include <libsolidity/parsing/Parser.h>
#include <libyul/AST.h>
#include <libyul/AsmParser.h>
#include <libyul/AsmPrinter.h>
#include <libyul/ObjectParser.h>
#include <libyul/Object.h>
#include <liblangutil/SourceReferenceFormatter.h>

#include <libyul/optimiser/Disambiguator.h>
#include <libyul/optimiser/OptimiserStep.h>
#include <libyul/optimiser/StackCompressor.h>
#include <libyul/optimiser/VarNameCleaner.h>
#include <libyul/optimiser/Suite.h>

#include <libyul/backends/evm/EVMDialect.h>

#include <libsolutil/JSON.h>

#include <libsolidity/interface/OptimiserSettings.h>
#include <liblangutil/CharStreamProvider.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/program_options.hpp>

#include <range/v3/action/sort.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/concat.hpp>
#include <range/v3/view/drop.hpp>
#include <range/v3/view/map.hpp>
#include <range/v3/view/set_algorithm.hpp>
#include <range/v3/view/stride.hpp>
#include <range/v3/view/transform.hpp>

#include <cctype>
#include <string>
#include <sstream>
#include <iostream>
#include <variant>

using namespace std;
using namespace solidity;
using namespace solidity::util;
using namespace solidity::langutil;
using namespace solidity::frontend;
using namespace solidity::yul;

namespace po = boost::program_options;

class YulOpti
{
public:
	static void printErrors(CharStream const& _charStream, ErrorList const& _errors)
	{
		SourceReferenceFormatter{
			cerr,
			SingletonCharStreamProvider(_charStream),
			true,
			false
		}.printErrorInformation(_errors);
	}

	shared_ptr<Object> getSubObject(shared_ptr<Object> const& _rootObject, string const& _path)
	{
		if (_path.empty())
			return _rootObject;

		auto pathToSubObject = _rootObject->pathToSubObject(YulString(_path));
		auto subObject = _rootObject;

		for (auto const& i: pathToSubObject)
			subObject = dynamic_pointer_cast<Object>(subObject->subObjects[i]);

		return subObject;
	}

	void parse(string const& _input, string const& _objectPath)
	{
		ErrorList errors;
		ErrorReporter errorReporter(errors);
		CharStream _charStream(_input, "");

		try
		{
			ObjectParser parser(errorReporter, m_dialect);

			auto scanner = make_shared<Scanner>(_charStream);
			auto content = parser.parse(scanner, false);

			if (content != nullptr)
				m_object = getSubObject(content, _objectPath);

			if (!m_object || !errorReporter.errors().empty())
			{
				cerr << "Error parsing source." << endl;
				printErrors(_charStream, errors);
				throw std::runtime_error("Could not parse source.");
			}

			analyze(errorReporter);
		}
		catch(...)
		{
			cerr << "Fatal error during parsing: " << endl;
			printErrors(_charStream, errors);
			throw;
		}
	}

	void printUsageBanner(
		map<char, string> const& _extraOptions,
		size_t _columns
	)
	{
		yulAssert(_columns > 0);
		auto const& optimiserSteps = OptimiserSuite::stepAbbreviationToNameMap();
		auto hasShorterString = [](auto const& a, auto const& b) { return a.second.size() < b.second.size(); };
		size_t longestDescriptionLength = std::max(
			max_element(optimiserSteps.begin(), optimiserSteps.end(), hasShorterString)->second.size(),
			max_element(_extraOptions.begin(), _extraOptions.end(), hasShorterString)->second.size()
		);

		vector<string> overlappingAbbreviations =
			ranges::views::set_intersection(_extraOptions | ranges::views::keys, optimiserSteps | ranges::views::keys) |
			ranges::views::transform([](char _abbreviation){ return string(1, _abbreviation); }) |
			ranges::to<vector>();

		yulAssert(
			overlappingAbbreviations.empty(),
			"ERROR: Conflict between yulopti controls and the following Yul optimizer step abbreviations: " +
			boost::join(overlappingAbbreviations, ", ") + ".\n"
			"This is most likely caused by someone adding a new step abbreviation to "
			"OptimiserSuite::stepNameToAbbreviationMap() and not realizing that it's used by yulopti.\n"
			"Please update the code to use a different character and recompile yulopti."
		);

		vector<tuple<char, string>> sortedOptions =
			ranges::views::concat(optimiserSteps, _extraOptions) |
			ranges::to<vector<tuple<char, string>>>() |
			ranges::actions::sort([](tuple<char, string> const& _a, tuple<char, string> const& _b) {
				return (
					!boost::algorithm::iequals(get<1>(_a), get<1>(_b)) ?
					boost::algorithm::lexicographical_compare(get<1>(_a), get<1>(_b), boost::algorithm::is_iless()) :
					toLower(get<0>(_a)) < toLower(get<0>(_b))
				);
			});

		yulAssert(sortedOptions.size() > 0);
		size_t rows = (sortedOptions.size() - 1) / _columns + 1;
		for (size_t row = 0; row < rows; ++row)
		{
			for (auto const& [key, name]: sortedOptions | ranges::views::drop(row) | ranges::views::stride(rows))
				cout << key << ": " << setw(static_cast<int>(longestDescriptionLength)) << setiosflags(ios::left) << name << " ";

			cout << endl;
		}
	}

	void objectApply(function<void(Object&)> _fn)
	{
		vector<shared_ptr<Object>> stack;
		stack.push_back(m_object);

		while (!stack.empty()) {
			auto object = stack.back();
			stack.pop_back();

			for (auto const& subObjectNode: object->subObjects) {
				auto subObject = dynamic_pointer_cast<Object>(subObjectNode);

				if (subObject != nullptr)
					stack.push_back(subObject);
			}

			_fn(*object);
		}
	}

	void analyze(ErrorReporter& errorReporter)
	{
		objectApply([&](Object& object) -> void {
			object.analysisInfo = make_shared<yul::AsmAnalysisInfo>();

			AsmAnalyzer analyzer(
				*object.analysisInfo,
				errorReporter,
				m_dialect,
				{},
				object.qualifiedDataNames()
			);

			bool success = analyzer.analyze(*object.code);
			yulAssert(success && !errorReporter.hasErrors(), "Invalid assembly/yul code.");
		});
	}

	void disambiguate()
	{
		objectApply([&](Object& object) -> void {
			object.code = make_shared<yul::Block>(
				std::get<yul::Block>(Disambiguator(m_dialect, *object.analysisInfo)(*object.code))
			);

			object.analysisInfo.reset();
		});
	}

	void runSequence(string_view _steps)
	{
		objectApply([&](Object& object) -> void {
			OptimiserSuite{*m_context}.runSequence(_steps, *object.code);
		});
	}

	void runVarNameCleaner()
	{
		objectApply([&](Object& object) -> void {
			VarNameCleaner::run(*m_context, *object.code);
		});
	}

	void runStackCompressor()
	{
		objectApply([&](Object& object) -> void {
			StackCompressor::run(m_dialect, object, true, 16);
		});
	}

	void parseAndPrint(string _source, string _objectPath)
	{
		parse(_source, _objectPath);
		cout << m_object->toString(&m_dialect) << endl;
	}

	void resetNameDispenser()
	{
		m_nameDispenser = make_shared<NameDispenser>(
			m_dialect,
			m_reservedIdentifiers
		);

		m_context = make_shared<OptimiserStepContext>(
			OptimiserStepContext{
				m_dialect,
				*m_nameDispenser,
				m_reservedIdentifiers,
				solidity::frontend::OptimiserSettings::standard().expectedExecutionsPerDeployment
			}
		);
	}

	void runSteps(string _source, string _objectPath, string _steps)
	{
		parse(_source, _objectPath);
		disambiguate();
		runSequence(_steps);
	}

	void runInteractive(string _source, string const& _objectPath, bool _disambiguated = false)
	{
		ErrorList errors;
		ErrorReporter errorReporter(errors);
		bool disambiguated = _disambiguated;

		parse(_source, _objectPath);

		while (true)
		{
			disambiguated = disambiguated || (disambiguate(), true);

			map<char, string> const& extraOptions = {
				// QUIT starts with a non-letter character on purpose to get it to show up on top of the list
				{'#', ">>> QUIT <<<"},
				{',', "VarNameCleaner"},
				{';', "StackCompressor"}
			};

			printUsageBanner(extraOptions, 4);
			cout << "? ";
			cout.flush();
			char option = static_cast<char>(readStandardInputChar());
			cout << ' ' << option << endl;

			try
			{
				switch (option)
				{
					case 4:
					case '#':
						return;
					case ',':
						runVarNameCleaner();
						// VarNameCleaner destroys the unique names guarantee of the disambiguator.
						disambiguated = false;
						break;
					case ';':
					{
						runStackCompressor();
						break;
					}
					default:
						runSequence(std::string_view(&option, 1));
				}

				resetNameDispenser();
				analyze(errorReporter);
			}
			catch (...)
			{
				cerr << endl << "Exception during optimiser step:" << endl;
				cerr << boost::current_exception_diagnostic_information() << endl;
			}
			cout << "----------------------" << endl;
			cout << m_object->toString(&m_dialect) << endl;
		}
	}

private:
	shared_ptr<yul::Object> m_object;
	Dialect const& m_dialect{EVMDialect::strictAssemblyForEVMObjects(EVMVersion{})};
	set<YulString> const m_reservedIdentifiers = {};
	shared_ptr<NameDispenser> m_nameDispenser = make_shared<NameDispenser>(
		m_dialect,
		m_reservedIdentifiers
	);
	shared_ptr<OptimiserStepContext> m_context = make_shared<OptimiserStepContext>(
		OptimiserStepContext{
			m_dialect,
			*m_nameDispenser,
			m_reservedIdentifiers,
			solidity::frontend::OptimiserSettings::standard().expectedExecutionsPerDeployment
		}
	);
};

int main(int argc, char** argv)
{
	try
	{
		bool nonInteractive = false;
		po::options_description options(
			R"(yulopti, yul optimizer exploration tool.
	Usage: yulopti [Options] <file>
	Reads <file> containing a yul object and applies optimizer steps to it,
	interactively read from stdin.
	In non-interactive mode a list of steps has to be provided.
	If <file> is -, yul code is read from stdin and run non-interactively.
	If <object> is provided then only the object matching the given path will be read.

	Allowed options)",
			po::options_description::m_default_line_length,
			po::options_description::m_default_line_length - 23);
		options.add_options()
			(
				"input-file",
				po::value<string>(),
				"input file"
			)
			(
				"steps",
				po::value<string>(),
				"steps to execute non-interactively"
			)
			(
				"object",
				po::value<string>(),
				"path to a yul object in the input"
			)
			(
				"non-interactive,n",
				po::bool_switch(&nonInteractive)->default_value(false),
				"stop after executing the provided steps"
			)
			("help,h", "Show this help screen.");

		// All positional options should be interpreted as input files
		po::positional_options_description filesPositions;
		filesPositions.add("input-file", 1);

		po::variables_map arguments;
		po::command_line_parser cmdLineParser(argc, argv);
		cmdLineParser.options(options).positional(filesPositions);
		po::store(cmdLineParser.run(), arguments);
		po::notify(arguments);

		if (arguments.count("help"))
		{
			cout << options;
			return 0;
		}

		string input;
		string objectPath;

		if (arguments.count("input-file"))
		{
			string filename = arguments["input-file"].as<string>();
			if (filename == "-")
			{
				nonInteractive = true;
				input = readUntilEnd(cin);
			}
			else
				input = readFileAsString(arguments["input-file"].as<string>());
		}
		else
		{
			cout << options;
			return 1;
		}

		if (nonInteractive && !arguments.count("steps"))
		{
			cout << options;
			return 1;
		}

		if (arguments.count("object"))
			objectPath = arguments["object"].as<string>();

		YulOpti yulOpti;
		bool disambiguated = false;

		if (!nonInteractive)
			yulOpti.parseAndPrint(input, objectPath);

		if (arguments.count("steps"))
		{
			string sequence = arguments["steps"].as<string>();
			if (!nonInteractive)
				cout << "----------------------" << endl;
			yulOpti.runSteps(input, objectPath, sequence);
			disambiguated = true;
		}
		if (!nonInteractive)
			yulOpti.runInteractive(input, objectPath, disambiguated);

		return 0;
	}
	catch (po::error const& _exception)
	{
		cerr << _exception.what() << endl;
		return 1;
	}
	catch (FileNotFound const& _exception)
	{
		cerr << "File not found:" << _exception.comment() << endl;
		return 1;
	}
	catch (NotAFile const& _exception)
	{
		cerr << "Not a regular file:" << _exception.comment() << endl;
		return 1;
	}
	catch(...)
	{
		cerr << endl << "Exception:" << endl;
		cerr << boost::current_exception_diagnostic_information() << endl;
		return 1;
	}
}
