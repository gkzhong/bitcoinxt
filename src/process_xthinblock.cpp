#include "process_xthinblock.h"
#include "blockheaderprocessor.h"
#include "consensus/validation.h"
#include "main.h" // Misbehaving, mapBlockIndex
#include "net.h"
#include "streams.h"
#include "thinblock.h"
#include "util.h"
#include "utilprocessmsg.h"
#include "xthin.h"


void XThinBlockProcessor::operator()(
        CDataStream& vRecv, const TxFinder& txfinder,
        uint64_t currMaxBlockSize, int activeChainHeight)
{
    XThinBlock block;
    vRecv >> block;

    uint256 hash = block.header.GetHash();

    LogPrintf("received xthinblock %s from peer=%d\n",
            hash.ToString(), worker.nodeID());

    try {
        block.selfValidate(currMaxBlockSize);
    }
    catch (const std::invalid_argument& e) {
        LogPrint(Log::BLOCK, "Invalid xthin block %s\n", e.what());
        rejectBlock(hash, e.what(), 20);
        return;
    }

    if (requestConnectHeaders(block.header, true)) {
        worker.stopWork(hash);
        return;
    }

    if (!setToWork(block.header, activeChainHeight))
        return;

    from.AddInventoryKnown(CInv(MSG_XTHINBLOCK, hash));

    try {
        XThinStub stub(block);
        worker.buildStub(stub, txfinder, from);
    }
    catch (const thinblock_error& e) {
        rejectBlock(hash, e.what(), 10);
        return;
    }

    if (!worker.isWorkingOn(hash)) {
        // Stub had enough data to finish
        // the block.
        return;
    }

    // Request missing
    std::vector<std::pair<int, ThinTx> > missing = worker.getTxsMissing(hash);

    XThinReRequest req;
    req.block = hash;

    for (auto& t : missing)
        req.txRequesting.insert(t.second.cheap());

    LogPrintf("re-requesting xthin %d missing transactions for %s from peer=%d\n",
            missing.size(), hash.ToString(), from.id);

    from.PushMessage("get_xblocktx", req);
}
