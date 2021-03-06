#pragma once

#include <iostream>
#include <map>
#include <string>

#define ZERO_TOLERANCE 0.00001

class NetworkStateOutOfRange : public std::exception {
	virtual const char *what() {
		return "A subtraction brought the value of a species below 0";
	}
};

struct DoubleOverflowException : public std::exception {
	const char *what() const throw() {
		return "One of the specie concentrations reached an invalid value. Try "
					 "decreasing the step size.";
	}
};

//! Class to represent a networkstate.
/*! The networkstate represents the current state of the network.
This is represented through a map. This map contains a string, namely
the speciename and a double, the concentration */
class NetworkState : public std::map<std::string, double> {
public:
	NetworkState() = default;
	NetworkState(const NetworkState &other);
	NetworkState DeepCopy();
	std::string PrintCsvHeader();
	std::string PrintCsvRow();
	void Print();
	double time = 0;
	double Diff(const NetworkState &other);
	void Verify();
	NetworkState operator+(const NetworkState &);
	NetworkState operator-(const NetworkState &);
};
