package org.mosslang.compiler.optimizer;

import java.util.HashSet;
import java.util.Set;

public class ObservableEffects {
    public boolean localCaptureRead = false;
    public boolean localMutation = false;
    public boolean domainRead = false;
    public boolean domainWrite = false;
    public boolean message = false;
    public boolean externalIo = false;
    public boolean fileio = false;
    public boolean fileioUnknown = false;
    public boolean mayPanic = false;
    public Set<String> raiseSet = new HashSet<>();
    public boolean mayDiverge = false;
    public boolean unresolved = false;

    public boolean isFusionSafe() {
        return !localMutation && !domainRead && !domainWrite && !message &&
               !externalIo && !fileio && !mayPanic && raiseSet.isEmpty() &&
               !unresolved;
    }

    public boolean isSafeToSkip() {
        return isFusionSafe() && !mayDiverge;
    }

    public void merge(ObservableEffects other) {
        if (other == null) return;
        localCaptureRead = localCaptureRead || other.localCaptureRead;
        localMutation = localMutation || other.localMutation;
        domainRead = domainRead || other.domainRead;
        domainWrite = domainWrite || other.domainWrite;
        message = message || other.message;
        externalIo = externalIo || other.externalIo;
        fileio = fileio || other.fileio;
        fileioUnknown = fileioUnknown || other.fileioUnknown;
        mayPanic = mayPanic || other.mayPanic;
        raiseSet.addAll(other.raiseSet);
        mayDiverge = mayDiverge || other.mayDiverge;
        unresolved = unresolved || other.unresolved;
    }
}
