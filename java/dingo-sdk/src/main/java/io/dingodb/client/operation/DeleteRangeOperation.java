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

import io.dingodb.client.OperationContext;
import io.dingodb.client.RouteTable;
import io.dingodb.sdk.common.RangeWithOptions;
import io.dingodb.sdk.common.codec.KeyValueCodec;
import io.dingodb.sdk.common.table.RangeDistribution;
import io.dingodb.sdk.common.table.Table;
import io.dingodb.sdk.common.utils.Any;
import io.dingodb.sdk.common.utils.ByteArrayUtils;
import io.dingodb.sdk.common.utils.ByteArrayUtils.ComparableByteArray;

import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.NavigableMap;
import java.util.NavigableSet;
import java.util.TreeSet;
import java.util.stream.Collectors;

import static io.dingodb.sdk.common.utils.Any.wrap;

public class DeleteRangeOperation implements Operation {


    private static final DeleteRangeOperation INSTANCE = new DeleteRangeOperation();

    private DeleteRangeOperation() {
    }

    public static DeleteRangeOperation getInstance() {
        return INSTANCE;
    }

    private Comparator<Task> getComparator() {
        return (e1, e2) -> ByteArrayUtils.compare(e1.<OpRange>parameters().getStartKey(), e2.<OpRange>parameters().getStartKey());
    }

    @Override
    public Fork fork(Any parameters, Table table, RouteTable routeTable) {
        try {
            KeyValueCodec codec = routeTable.getCodec();
            OpKeyRange keyRange = parameters.getValue();
            List<Object> startKey = keyRange.start.getUserKey();
            List<Object> endKey = keyRange.end.getUserKey();
            OpRange range = new OpRange(
                codec.encodeKeyPrefix(startKey.toArray(new Object[table.getColumns().size()]), startKey.size()),
                codec.encodeKeyPrefix(endKey.toArray(new Object[table.getColumns().size()]), endKey.size()),
                keyRange.withStart,
                keyRange.withEnd
            );
            NavigableMap<ComparableByteArray, RangeDistribution> rangeDistribution = routeTable.getRangeDistribution();
            NavigableSet<Task> subTasks = (rangeDistribution.size() == 1 ? rangeDistribution : rangeDistribution
                .subMap(
                    rangeDistribution.floorKey(new ComparableByteArray(range.getStartKey())), true,
                    rangeDistribution.floorKey(new ComparableByteArray(range.getRange().getEndKey())), true
                )).values().stream()
                .map(rd -> new Task(rd.getId(),
                    wrap(new OpRange(rd.getRange().getStartKey(), rd.getRange().getEndKey(), true, false))
                ))
                .collect(Collectors.toCollection(() -> new TreeSet<>(getComparator())));
            Task task = subTasks.pollFirst();
            if (task == null) {
                return new Fork(new long[0], subTasks, true);
            }
            OpRange taskRange = task.parameters();
            subTasks.add(new Task(
                task.getRegionId(),
                wrap(new OpRange(range.getStartKey(), taskRange.getEndKey(), range.withStart, taskRange.withEnd))
            ));
            task = subTasks.pollLast();
            taskRange = task.parameters();
            subTasks.add(new Task(
                task.getRegionId(),
                wrap(new OpRange(taskRange.getStartKey(), range.getEndKey(), taskRange.withStart, range.withEnd))
            ));
            return new Fork(new long[subTasks.size()], subTasks, true);

        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    @Override
    public void exec(OperationContext context) {
        OpRange range = context.parameters();
        context.<long[]>result()[context.getSeq()] = context.getStoreService()
            .kvDeleteRange(context.getTableId(), context.getRegionId(), new RangeWithOptions(range.range, range.withStart, range.withEnd));
    }

    @Override
    public <R> R reduce(Fork context) {
        return (R) (Long) Arrays.stream(context.<long[]>result()).reduce(Long::sum).orElse(0L);
    }
}