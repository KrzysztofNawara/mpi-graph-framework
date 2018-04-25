//
// Created by blueeyedhush on 14.07.17.
//

#include <gtest/gtest.h>
#include <glog/logging.h>
#include <mpi.h>
#include <representations/RoundRobin2DPartition.h>
#include <utils/TestUtils.h>

typedef int TestLocalId;
typedef int TestNumId;
#define NUM_MPI_TYPE MPI_INT
using G = RoundRobin2DPartition<TestLocalId, TestNumId>;
using ABCPGid = typename G::GidType;

class CommHelper {
public:
	CommHelper(std::vector<OriginalVertexId>& originalVids,
	           std::vector<TestNumId>& mappedVids,
	           MPI_Comm shmComm,
	           NodeId shmRank) : shmcomm(shmComm), shmRank(shmRank)
	{
		/* build mapping mapped -> original */
		assert(originalVids.size() == mappedVids.size());
		auto vCount = originalVids.size();
		for(size_t i = 0; i < vCount; i++)
			vmap.emplace(mappedVids[i], originalVids[i]);

		/* allocate window */
		TestNumId *winData = nullptr;
		/* max amount of edges is vcount^2-1, but we also need first one for counting */
		// @todo: types might not fit! look at it! (add assert?)
		MPI_Win_allocate_shared(2*vCount*vCount, sizeof(TestNumId), MPI_INFO_NULL, shmcomm, &winData, &win);
		MPI_Win_lock_all(0, win);
	}

	~CommHelper() {
		MPI_Win_unlock_all(win);
		MPI_Win_free(&win);
	}

	/* writing */
	void appendEdge(TestNumId startV, TestNumId endV) {
		edgeDatas.push_back(startV);
		edgeDatas.push_back(endV);
	}

	void finishTransfers() {
		assert(edgeDatas.size() % 2 == 0);
		unsigned long edgeCount = edgeDatas.size()/2;

		auto* data = edgeDatas.data();
		MPI_Put(&edgeCount, 1, MPI_UNSIGNED_LONG, shmRank, 0, 1, MPI_UNSIGNED_LONG, win);

		for(unsigned long i = 0; i < edgeCount; i++) {
			MPI_Put(data + 2*i, 1, NUM_MPI_TYPE, shmRank, 1 + 2*i, 1, NUM_MPI_TYPE, win);
			MPI_Put(data + 2*i + 1, 1, NUM_MPI_TYPE, shmRank, 1 + 2*i + 1, 1, NUM_MPI_TYPE, win);
		}

		MPI_Win_flush_all(win);
	}

	/* reading */
	std::set<std::pair<OriginalVertexId, OriginalVertexId>> getEdgesFrom(NodeId id) {
		/* here we use global ranks, but they should be the same */

		unsigned long edgeCount = 0;
		MPI_Get(&edgeCount, 1, NUM_MPI_TYPE, id, 0, 1, NUM_MPI_TYPE, win);
		MPI_Win_flush(id, win);

		auto *recvBuffer = new TestNumId[edgeCount*2];
		MPI_Get(recvBuffer, edgeCount*2, NUM_MPI_TYPE, id, 1, edgeCount*2, NUM_MPI_TYPE, win);
		MPI_Win_flush(id, win);

		std::set<std::pair<OriginalVertexId, OriginalVertexId>> edges;
		for(unsigned long i = 0; i < edgeCount; i++) {
			auto first = vmap[recvBuffer[2*i]];
			auto second = vmap[recvBuffer[2*i] + 1];
			edges.emplace(std::make_pair(first, second));
		}

		delete[] recvBuffer;

		return edges;
	};

private:
	std::unordered_map<TestNumId, OriginalVertexId> vmap;
	std::vector<TestNumId> edgeDatas;
	MPI_Comm shmcomm;
	MPI_Win win;
	NodeId shmRank;
};

template <typename TGraphHandle>
void representationTest(std::function<TGraphHandle(NodeId /*size*/, NodeId /*rank*/)> ghSupplier,
                        std::vector<OriginalVertexId> vertexIds,
                        std::set<std::pair<OriginalVertexId, OriginalVertexId>> expectedEdges) {
	int rank = -1;
	int size = -1;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	LOG(INFO) << "Initialized MPI";

	auto builder = ghSupplier(size, rank);
	auto& gp = builder.getGraph();
	LOG(INFO) << "Loaded graph from file";

	std::vector<TestNumId> numIds;
	for(auto gid: builder.getConvertedVertices())
		numIds.push_back(gp.toNumeric(gid));

	/* get shared memory communicator */
	MPI_Comm shmcomm;
	int shmSize;
	int shmRank;

	MPI_Comm_split_type (MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED,0, MPI_INFO_NULL, &shmcomm);
	MPI_Comm_size(shmcomm, &shmSize);
	MPI_Comm_rank(shmcomm, &shmRank);

	/* ensure all nodes work locally */
	ASSERT_TRUE(shmSize == size);

	CommHelper commHelper(vertexIds, numIds, MPI_COMM_WORLD, shmRank);

	/* iterate masters & shadows, (source, target) pairs to file */
	gp.foreachMasterVertex([&](const auto startLid) {
		gp.foreachNeighbouringVertex(startLid, [&](const auto endGid) {
			commHelper.appendEdge(gp.toNumeric(startLid), gp.toNumeric(endGid));
			return CONTINUE;
		});
		return CONTINUE;
	});

	gp.foreachShadowVertex([&](auto startLid, auto startGid) {
		gp.foreachNeighbouringVertex(startLid, [&](auto endGid) {
			commHelper.appendEdge(gp.toNumeric(startLid), gp.toNumeric(endGid));
			return CONTINUE;
		});
		return CONTINUE;
	});

	commHelper.finishTransfers();

	MPI_Barrier(MPI_COMM_WORLD);

	/* gather edges */
	std::set<std::pair<OriginalVertexId, OriginalVertexId>> actualEdges;

	for(NodeId nodeId = 0; nodeId < size; nodeId++) {
		auto edgesFromNode = commHelper.getEdgesFrom(nodeId);
		actualEdges.insert(edgesFromNode.begin(), edgesFromNode.end());
	}

	/* compare against */
	ASSERT_EQ(actualEdges, expectedEdges);
}

TEST(Rr2dRepresentation, PartitioningPreservesGraphStructure) {
	std::vector<OriginalVertexId> vertexIds({0, 1, 2, 3});

	std::set<std::pair<OriginalVertexId, OriginalVertexId>> expectedEdges({
			                                                                      {0,1},{0,2},{0,3},
			                                                                      {1,0},{1,2},{1,3},
			                                                                      {2,0},{2,1},{2,3},
			                                                                      {3,0},{3,1},{3,2}
	                                                                      });

	representationTest<ABCGraphHandle<TestLocalId, TestNumId>>(
			[&](NodeId size, NodeId rank) {
				return ABCGraphHandle<TestLocalId, TestNumId>("resources/test/SimpleTestGraph.adjl", size, rank, vertexIds);
			}, vertexIds, expectedEdges);
}
