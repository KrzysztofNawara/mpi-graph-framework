
#include <gtest/gtest.h>
#include <mpi.h>
#include <utils/TestUtils.h>
#include <Runner.h>
#include <representations/AdjacencyListHashPartition.h>
#include <algorithms/bfs/Bfs1CommsRound.h>
#include <algorithms/bfs/BfsFixedMessage.h>
#include <algorithms/bfs/BfsVarMessage.h>
#include <validators/BfsValidator.h>

template <
		template<typename, typename> class TGraphBuilder,
		template<typename> class TBfsAlgo,
		template<typename> class TBfsValidator>
static void executeTest(std::string graphPath, ull originalRootId)
{
	auto *graphHandle = new TGraphBuilder<int,int>(graphPath, {originalRootId});
	auto& g = graphHandle->getGraph();

	using TGP = typename TGraphBuilder<int,int>::GPType;
	using TResult = typename TBfsAlgo<TGP>::ResultType;

	auto bfsRoot = graphHandle->getConvertedVertices()[0];
	auto* algo = new TBfsAlgo<TGP>(bfsRoot);
	auto* validator = new TBfsValidator<TGP>(bfsRoot);
	AlgorithmExecutionResult r = runAndCheck(&g, *algo, *validator);

	delete validator;
	delete algo;
	delete graphHandle;
	ASSERT_TRUE(r.algorithmStatus);
	ASSERT_TRUE(r.validatorStatus);
}

TEST(Bfs_Mp_FixedMsgLen_1D_2CommRounds, FindsCorrectSolutionForSTG) {
	executeTest<
			ALHGraphHandle,
			Bfs_Mp_FixedMsgLen_1D_2CommRounds,
			BfsValidator>("resources/test/SimpleTestGraph.adjl", 0);
}

TEST(Bfs_Mp_FixedMsgLen_1D_2CommRounds, FindsCorrectSolutionForComplete50) {
	executeTest<
			ALHGraphHandle,
			Bfs_Mp_FixedMsgLen_1D_2CommRounds,
			BfsValidator>("resources/test/complete50.adjl", 0);
}

TEST(Bfs_Mp_VarMsgLen_1D_2CommRounds, FindsCorrectSolutionForSTG) {
	executeTest<
			ALHGraphHandle,
			Bfs_Mp_VarMsgLen_1D_2CommRounds,
			BfsValidator>("resources/test/SimpleTestGraph.adjl", 0);
}

TEST(Bfs_Mp_VarMsgLen_1D_2CommRounds, FindsCorrectSolutionForComplete50) {
	executeTest<
			ALHGraphHandle,
			Bfs_Mp_VarMsgLen_1D_2CommRounds,
			BfsValidator>("resources/test/complete50.adjl", 0);
}

TEST(Bfs_Mp_VarMsgLen_1D_1CommsTag, FindsCorrectSolutionForSTG) {
	executeTest<
			ALHGraphHandle,
			Bfs_Mp_VarMsgLen_1D_1CommsTag,
			BfsValidator>("resources/test/SimpleTestGraph.adjl", 0);
}

TEST(Bfs_Mp_VarMsgLen_1D_1CommsTag, FindsCorrectSolutionForComplete50) {
	executeTest<
			ALHGraphHandle,
			Bfs_Mp_VarMsgLen_1D_1CommsTag,
			BfsValidator>("resources/test/complete50.adjl", 0);
}