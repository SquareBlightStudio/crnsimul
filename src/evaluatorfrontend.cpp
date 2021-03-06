#include "evaluatorfrontend.h"
#include <boost/lockfree/queue.hpp>
#include <mutex>
#include <thread>

void EvaluatorFrontend::PrintCsvString() {
	*csvStream << initNetworkState.PrintCsvHeader();
	*csvStream << initNetworkState.PrintCsvRow();
	for (auto &state : states) {
		*csvStream << state.PrintCsvRow();
	}
}

void EvaluatorFrontend::PrintCsv() {
	PrintCsvString();
	csvFileStream.open(csvFilename);
	csvFileStream << csvStream->rdbuf();
	csvFileStream.close();
	std::cout << "Printed to file " << csvFilename << std::endl;
}

std::vector<std::string> EvaluatorFrontend::GeneratePlotString() {
	std::vector<std::string> plotStrings;
	if (!desiredChemicals.empty()) {
		for (const auto &specieName : desiredChemicals) {
			auto toPlot = initNetworkState.find(specieName);
			if (toPlot == initNetworkState.end()) {
				throw std::runtime_error("Tried to plot chemical not in CRN");
			}
			plotStrings.push_back(toPlot->first);
		}
	} else {
		for (auto &species : initNetworkState) {
			plotStrings.push_back(species.first);
		}
	}
	return plotStrings;
}

void EvaluatorFrontend::Plot() {
	std::vector<std::string> plotStrings = GeneratePlotString();
	gp << "plot";
	int plotStringsSize = static_cast<int>(plotStrings.size());
	for (int i = 0; i < plotStringsSize; i++) {
		std::string title = plotStrings[i];
		gp << " '-' with lines title '" << title << "'";
		if (i != plotStringsSize - 1)
			gp << ", ";
	}
	gp << "\n";
	for (std::string &toPlot : plotStrings) {
		std::cout.precision(17);
		for (auto &state : states) {
			gp << state.time << " " << state[toPlot] << "\n";
		}
		gp << "e";
	}
}

void EvaluatorFrontend::FuncRunner() {
	initNetworkState = drv->network.initNetworkState;

	if (rtPlot) {
		RTPlotInit();
		RunRTEvaluator();
	} else {
		RunEvaluator();
	}

	if (print) {
		if (csvStream != nullptr)
			PrintCsv();
		else
			Help(streamNullPtr);
	}
	if (plot)
		Plot();
}

bool EvaluatorFrontend::EvaluatorFunc() {
	if (!evaluator->IsFinished()) {
		states.push_back(evaluator->GetNextNetworkState());
		if (printStd) {
			std::cout << states.back().PrintCsvRow();
		}
	} else {
		return false;
	}

	return true;
}

//! @file
// [runeval]
void EvaluatorFrontend::RunEvaluator() {
	while (EvaluatorFunc())
		;
}
// [runeval]

void EvaluatorFrontend::RunRTEvaluator() {
	auto startTime = std::chrono::steady_clock::now();
	auto endTime = std::chrono::steady_clock::now();

	boost::lockfree::queue<NetworkState *> stateQueue(1024);

	if (!stateQueue.is_lock_free()) {
		throw std::runtime_error("Queue is not lockfree");
	}

	bool plotterRunning = true;

	std::thread threadObj([this, &stateQueue, &plotterRunning] {
		while (!evaluator->IsFinished() && plotterRunning) {
			states.push_back(evaluator->GetNextNetworkState());
			if (printStd)
				std::cout << states.back().PrintCsvRow();
			NetworkState *copy = new NetworkState(states.back());
			if (!stateQueue.push(copy)) {
				throw std::runtime_error("Push failed");
			}
		}
	});

	bool plotUpdated = false;

	while (!mPlot->RunPlot()) {
		endTime = std::chrono::steady_clock::now();
		auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
				endTime - startTime);
		auto updateTime = std::chrono::milliseconds(UpdateRate);

		while (!stateQueue.empty()) {
			NetworkState *state = nullptr;
			bool res = stateQueue.pop(state);
			if (!res)
				throw std::runtime_error("Could not pop from state queue");

			for (int i = 0; i < toPlot.size(); i++) {
				auto &name = toPlot[i].Name;
				auto &time = state->time;
				double value = 0;
				value = state->at(toPlot[i].Name);
				toPlot[i].Function.push_back((OpenRTP::Point){
						static_cast<float>(time), static_cast<float>(value)});
			}
			delete state;
			plotUpdated = true;
		}

		if (plotUpdated && elapsedTime > updateTime) {
			startTime = std::chrono::steady_clock::now();
			mPlot->UpdatePlot();
			plotUpdated = false;
		}
	}
	plotterRunning = false;
	threadObj.join();
}

void EvaluatorFrontend::RTPlotInit() {
	OpenRTP::InitStruct Init{"Window", "Y", "X"};

	std::vector<std::string> plotStrings = GeneratePlotString();
	int plotStringsSize = static_cast<int>(plotStrings.size());
	for (int i = 0; i < plotStringsSize; i++) {
		std::string title = plotStrings[i];
		toPlot.push_back(title);
		toPlot[i].Function.push_back(
				(OpenRTP::Point){static_cast<float>(initNetworkState.time),
												 static_cast<float>(initNetworkState[toPlot[i].Name])});
	}

	mPlot = new OpenRTP::Plotter(Init, &toPlot);
	mPlot->Init();
}

void EvaluatorFrontend::Help(ErrorCode errorCode) {
	if (errorCode == fileError) {
		std::cout << "Error: No file for parsing" << std::endl;
	} else if (errorCode == rError) {
		std::cout << "Warning: Perhaps -r flag was forgotten?" << std::endl;
	} else if (errorCode == pError) {
		std::cout << "Error while parsing" << std::endl;
	} else if (errorCode == streamNullPtr)
		std::cout << "filestreampointer was nullptr. Please try again" << std::endl;
	std::string helperstring =
			"Usage: crnsimul [OPTIONS] filename\n"
			"Option types:\n"
			"Default evaluator is Euler evaluator\n"
			"\t-m to to enable markov evaluator\n"
			"\t-P enable plotting in gnuplot\n"
			"\t--stdout print states to stdout\n"
			"\t-O <filename> output result to file, -r required\n"
			"\t-S <step size> set stepsize for Euler evaluator\n"
			"\t-T <threshold size> set the Euler evaluator threshold\n"
			"\t-t to set threshold for markovEvaluator\n"
			"\t\tIf -t 0 is provided the markovEvaluator will continue forever\n"
			"\t\tand print the result as it is going to stdout\n"
			"\t\tIt can then be stopped with ctrl+c\n"
			"\t-C to select desired chemicals to plot\n"
			"\t\tThese are seperated with the commaseperator\n";
	"\t--stdout parameter prints the output to stdout\n"
	"\t\tIt is recommended to use this when markovThreshold i set to 0\n";
	"\t-h Display help options";
	std::cout << helperstring << std::endl;
}
