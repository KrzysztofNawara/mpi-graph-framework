//
// Created by blueeyedhush on 05.10.17.
//

#ifndef FRAMEWORK_RUNNER_H
#define FRAMEWORK_RUNNER_H

#include <mpi.h>
#include "GraphPartition.h"
#include "Algorithm.h"
#include "Validator.h"

struct AlgorithmExecutionResult {
	bool algorithmStatus = false;
	bool validatorStatus = false;
};

template<class TGraphPartition, typename TAlgorithm, typename TValidator>
AlgorithmExecutionResult runAndCheck(TGraphPartition* graph, TAlgorithm& algorithm, TValidator& validator) {
	AlgorithmExecutionResult r;

	r.algorithmStatus = algorithm.run(graph);

	MPI_Barrier(MPI_COMM_WORLD);

	auto solution = algorithm.getResult();
	r.validatorStatus = validator.validate(graph, solution);

	return r;
}

class Assembly {
	virtual void run() = 0;
	virtual ~Assembly() {};
};

/**
 *
 * This class doesn't perform any cleanup of resources that were passed it - however, you can
 * assume that as soon as run returns, they can be cleaned up
 *
 * @tparam TGHandle
 * @tparam TAlgorithm
 * @tparam TValidator
 */
template <typename TGHandle, template <typename> class TAlgorithm, template <typename> class TValidator>
class AlgorithmAssembly : Assembly {
	using G = typename TGHandle::GPType;

public:
	virtual void run() override {
		TGHandle& handle = getHandle();
		auto graph = handle->getGraph();

		TAlgorithm<G>& algorithm = getAlgorithm(handle);
		bool algorithmStatus = algorithm.run(graph);

		MPI_Barrier(MPI_COMM_WORLD);

		auto solution = algorithm.getResult();

		TValidator<G>& validator = getValidator(handle, algorithm);
		bool validationStatus = validator.validate(graph, solution);

		if (!algorithmStatus) {
			LOG(ERROR) << "Error occured while executing algorithm";
		} else {
			LOG(INFO) << "Algorithm terminated successfully";
		}

		if(!validationStatus) {
			LOG(ERROR) << "Validation failure";
		} else {
			LOG(INFO) << "Validation success";
		}

		handle->releaseGraph();
	}

protected:
	virtual TGHandle& getHandle() = 0;
	virtual TAlgorithm<G>& getAlgorithm(TGHandle&) = 0;
	virtual TValidator<G>& getValidator(TGHandle&, TAlgorithm<G>&) = 0;
};

#endif //FRAMEWORK_RUNNER_H
