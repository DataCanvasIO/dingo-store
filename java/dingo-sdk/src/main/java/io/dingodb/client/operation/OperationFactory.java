/*
 * Copyright 2021 DataCanvas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package io.dingodb.client.operation;

import io.dingodb.client.IStoreOperation;
import io.dingodb.client.StoreOperationType;
import io.dingodb.client.operation.GetOperation;
import io.dingodb.client.operation.PutOperation;

public final class OperationFactory {

    public static IStoreOperation getStoreOperation(StoreOperationType type) {
        switch (type) {
            case GET:
                return GetOperation.getInstance();
            case PUT:
                return PutOperation.getInstance();
            case PUT_IF_ABSENT:
                return PutIfAbsentOperation.getInstance();
            default:
                return null;
        }
    }
}