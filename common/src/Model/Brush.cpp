/*
 Copyright (C) 2010-2017 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Brush.h"

#include "Exceptions.h"
#include "FloatType.h"
#include "Polyhedron.h"
#include "Polyhedron_Matcher.h"
#include "Model/BrushFace.h"
#include "Model/BrushGeometry.h"
#include "Model/ModelFactory.h"
#include "Model/TexCoordSystem.h"

#include <kdl/vector_utils.h>

#include <vecmath/intersection.h>
#include <vecmath/vec.h>
#include <vecmath/vec_ext.h>
#include <vecmath/mat.h>
#include <vecmath/mat_ext.h>
#include <vecmath/segment.h>
#include <vecmath/polygon.h>
#include <vecmath/util.h>

#include <algorithm> // for std::remove
#include <iterator>
#include <set>
#include <string>
#include <vector>
#include <unordered_map>

namespace TrenchBroom {
    namespace Model {
        class Brush::AddFaceToGeometryCallback : public BrushGeometry::Callback {
        private:
            BrushFace* m_addedFace;
        public:
            explicit AddFaceToGeometryCallback(BrushFace* addedFace) :
            m_addedFace(addedFace) {
                ensure(m_addedFace != nullptr, "addedFace is null");
            }

            void faceWasCreated(BrushFaceGeometry* face) override {
                assert(m_addedFace != nullptr);
                m_addedFace->setGeometry(face);
                m_addedFace = nullptr;
            }

            void faceWasSplit(BrushFaceGeometry* original, BrushFaceGeometry* clone) override {
                auto* brushFace = original->payload();
                if (brushFace != nullptr) {
                    auto* brushFaceClone = brushFace->clone();
                    brushFaceClone->setGeometry(clone);
                }
            }

            void faceWillBeDeleted(BrushFaceGeometry* face) override {
                auto* brushFace = face->payload();
                delete brushFace;
            }
        };

        class Brush::HealEdgesCallback : public BrushGeometry::Callback {
        public:
            void facesWillBeMerged(BrushFaceGeometry*, BrushFaceGeometry* geometryToDelete) override {
                auto* faceToDelete = geometryToDelete->payload();
                delete faceToDelete;
            }

            void faceWillBeDeleted(BrushFaceGeometry* face) override {
                auto* brushFace = face->payload();
                delete brushFace;
            }
        };

        class Brush::AddFacesToGeometry {
        private:
            BrushGeometry& m_geometry;
            bool m_brushEmpty;
            bool m_brushValid;
        public:
            AddFacesToGeometry(BrushGeometry& geometry, std::vector<BrushFace*> facesToAdd) :
            m_geometry(geometry),
            m_brushEmpty(false),
            m_brushValid(true) {
                // sort the faces by the weight of their plane normals like QBSP does
                Model::BrushFace::sortFaces(facesToAdd);

                for (auto it = std::begin(facesToAdd), end = std::end(facesToAdd); it != end && !m_brushEmpty; ++it) {
                    auto* brushFace = *it;
                    AddFaceToGeometryCallback addCallback(brushFace);
                    const auto result = m_geometry.clip(brushFace->boundary(), addCallback);
                    m_brushEmpty = result.empty();
                }
                if (!m_brushEmpty && m_brushValid) {
                    m_geometry.correctVertexPositions();

                    HealEdgesCallback healCallback;
                    m_brushValid = m_geometry.healEdges(healCallback);
                }
            }

            bool brushEmpty() const {
                return m_brushEmpty;
            }

            bool brushValid() const {
                return m_brushValid;
            }
        };

        class Brush::QueryCallback : public BrushGeometry::Callback {
        public:
            vm::plane3 getPlane(const BrushFaceGeometry* face) const override {
                return face->payload()->boundary();
            }
        };

        class Brush::CopyCallback : public BrushGeometry::CopyCallback {
        private:
            const std::unordered_map<const BrushFace*, BrushFace*>& m_faceMap;
        public:
            explicit CopyCallback(const std::unordered_map<const BrushFace*, BrushFace*>& faceMap) :
            m_faceMap(faceMap) {}

            void faceWasCopied(const BrushFaceGeometry* original, BrushFaceGeometry* copy) override {
                auto it = m_faceMap.find(original->payload());
                ensure(it != std::end(m_faceMap), "face payload not found");

                auto* face = it->second;
                copy->setPayload(face);
                face->setGeometry(copy);
            }
        };

        Brush::Brush() :
        m_geometry(nullptr),
        m_transparent(false) {}

        Brush::Brush(const vm::bbox3& worldBounds, const std::vector<BrushFace*>& faces) :
        m_geometry(nullptr),
        m_transparent(false) {
            addFaces(faces);
            try {
                buildGeometry(worldBounds);
            } catch (const GeometryException&) {
                cleanup();
                throw;
            }
        }

        Brush::Brush(const Brush& other) :
        m_geometry(nullptr),
        m_transparent(other.m_transparent) {
            auto faceMap = std::unordered_map<const BrushFace*, BrushFace*>();
            m_faces.reserve(other.m_faces.size());
            for (const auto* face : other.m_faces) {
                auto* faceClone = face->clone();
                addFace(faceClone);
                faceMap.insert(std::make_pair(face, faceClone));
            }

            if (other.m_geometry != nullptr) {
                CopyCallback callback(faceMap);
                m_geometry = new BrushGeometry(*other.m_geometry, callback);
            }
        }

        Brush::Brush(Brush&& other) noexcept :
        m_faces(std::move(other.m_faces)),
        m_geometry(other.m_geometry),
        m_transparent(other.m_transparent) {
            other.m_geometry = nullptr;
        }

        Brush& Brush::operator=(Brush other) noexcept {
            using std::swap;
            swap(*this, other);
            return *this;
        }

        void swap(Brush& lhs, Brush& rhs) noexcept {
            using std::swap;
            swap(lhs.m_faces, rhs.m_faces);
            swap(lhs.m_geometry, rhs.m_geometry);
            swap(lhs.m_transparent, rhs.m_transparent);
        }

        Brush::~Brush() {
            cleanup();
        }

        void Brush::cleanup() {
            delete m_geometry;
            m_geometry = nullptr;
            kdl::vec_clear_and_delete(m_faces);
        }

        const vm::bbox3& Brush::bounds() const {
            ensure(m_geometry != nullptr, "geometry is null");
            return m_geometry->bounds();
        }

        std::optional<size_t> Brush::findFace(const std::string& textureName) const {
            return kdl::vec_index_of(m_faces, [&](const BrushFace* face) { return face->attributes().textureName() == textureName; });
        }

        std::optional<size_t> Brush::findFace(const vm::vec3& normal) const {
            return kdl::vec_index_of(m_faces, [&](const BrushFace* face) { return vm::is_equal(face->boundary().normal, normal, vm::C::almost_zero()); });
        }

        std::optional<size_t> Brush::findFace(const vm::plane3& boundary) const {
            return kdl::vec_index_of(m_faces, [&](const BrushFace* face) { return vm::is_equal(face->boundary(), boundary, vm::C::almost_zero()); });
        }

        std::optional<size_t> Brush::findFace(const vm::polygon3& vertices, const FloatType epsilon) const {
            return kdl::vec_index_of(m_faces, [&](const BrushFace* face) { return face->hasVertices(vertices, epsilon); });
        }

        std::optional<size_t> Brush::findFace(const std::vector<vm::polygon3>& candidates, const FloatType epsilon) const {
            for (const auto& candidate : candidates) {
                if (const auto faceIndex = findFace(candidate, epsilon)) {
                    return faceIndex;
                }
            }
            return std::nullopt;
        }

        std::optional<size_t> Brush::findFace(const BrushFace* face) const {
            return kdl::vec_index_of(m_faces, face);
        }

        const BrushFace* Brush::face(const size_t index) const {
            assert(index < faceCount());
            return m_faces[index];
        }

        BrushFace* Brush::face(const size_t index) {
            assert(index < faceCount());
            return m_faces[index];
        }

        size_t Brush::faceCount() const {
            return m_faces.size();
        }

        std::vector<const BrushFace*> Brush::faces() const {
            return kdl::vec_transform(m_faces, [](const auto* f) { return f; });
        }

        const std::vector<BrushFace*>& Brush::faces() {
            return m_faces;
        }

        bool Brush::closed() const {
            ensure(m_geometry != nullptr, "geometry is null");
            return m_geometry->closed();
        }

        bool Brush::fullySpecified() const {
            ensure(m_geometry != nullptr, "geometry is null");

            for (auto* current : m_geometry->faces()) {
                if (current->payload() == nullptr) {
                    return false;
                }
            }
            return true;
        }

        void Brush::addFaces(const std::vector<BrushFace*>& faces) {
            addFaces(std::begin(faces), std::end(faces), faces.size());
        }

        void Brush::addFace(BrushFace* face) {
            ensure(face != nullptr, "face is null");
            assert(!kdl::vec_contains(m_faces, face));
            m_faces.push_back(face);
        }

        void Brush::cloneFaceAttributesFrom(const Brush& brush) {
            for (auto* destination : m_faces) {
                if (const auto sourceIndex = brush.findFace(destination->boundary())) {
                    const auto* source = brush.face(*sourceIndex);
                    destination->setAttributes(source->attributes());

                    auto snapshot = source->takeTexCoordSystemSnapshot();
                    if (snapshot != nullptr) {
                        destination->copyTexCoordSystemFromFace(*snapshot, source->attributes().takeSnapshot(), source->boundary(), WrapStyle::Projection);
                    }
                }
            }
        }

        void Brush::cloneInvertedFaceAttributesFrom(const Brush& brush) {
            for (auto* destination : m_faces) {
                if (const auto sourceIndex = brush.findFace(destination->boundary().flip())) {
                    const auto* source = brush.face(*sourceIndex);
                    // Todo: invert the face attributes?
                    destination->setAttributes(source->attributes());

                    auto snapshot = source->takeTexCoordSystemSnapshot();
                    if (snapshot != nullptr) {
                        destination->copyTexCoordSystemFromFace(*snapshot, source->attributes().takeSnapshot(), destination->boundary(), WrapStyle::Projection);
                    }
                }
            }
        }

        bool Brush::clip(const vm::bbox3& worldBounds, BrushFace* face) {
            try {
                addFace(face);
                rebuildGeometry(worldBounds);
                return !m_faces.empty();
            } catch (GeometryException&) {
                return false;
            }
        }

        bool Brush::canMoveBoundary(const vm::bbox3& worldBounds, const size_t faceIndex, const vm::vec3& delta) const {
            const auto* face = this->face(faceIndex);
            auto* testFace = face->clone();
            testFace->transform(vm::translation_matrix(delta), false);

            std::vector<BrushFace*> testFaces;
            testFaces.push_back(testFace);

            for (auto* brushFace : m_faces) {
                if (brushFace != face) {
                    testFaces.push_back(brushFace->clone());
                }
            }

            try {
                const auto testBrush = Brush(worldBounds, testFaces);
                const auto inWorldBounds = worldBounds.contains(testBrush.bounds());
                const auto closed = testBrush.closed();
                const auto allFaces = testBrush.faceCount() == testFaces.size();

                return inWorldBounds && closed && allFaces;
            } catch (const GeometryException&) {
                return false;
            }
        }

        void Brush::moveBoundary(const vm::bbox3& worldBounds, const size_t faceIndex, const vm::vec3& delta, const bool lockTexture) {
            assert(canMoveBoundary(worldBounds, faceIndex, delta));

            auto* face = this->face(faceIndex);
            face->transform(vm::translation_matrix(delta), lockTexture);
            rebuildGeometry(worldBounds);
        }

        bool Brush::canExpand(const vm::bbox3& worldBounds, const FloatType delta, const bool lockTexture) const {
            auto testBrush = Brush(*this);
            return testBrush.expand(worldBounds, delta, lockTexture);
        }

        bool Brush::expand(const vm::bbox3& worldBounds, const FloatType delta, const bool lockTexture) {
            // move the faces
            for (BrushFace* face : m_faces) {
                const vm::vec3 moveAmount = face->boundary().normal * delta;
                face->transform(vm::translation_matrix(moveAmount), lockTexture);
            }

            // rebuild geometry
            try {
                rebuildGeometry(worldBounds);
                return !m_faces.empty();
            } catch (GeometryException&) {
                return false;
            }
        }

        size_t Brush::vertexCount() const {
            ensure(m_geometry != nullptr, "geometry is null");
            return m_geometry->vertexCount();
        }

        const Brush::VertexList& Brush::vertices() const {
            ensure(m_geometry != nullptr, "geometry is null");
            return m_geometry->vertices();
        }

        const std::vector<vm::vec3> Brush::vertexPositions() const {
            ensure(m_geometry != nullptr, "geometry is null");
            return m_geometry->vertexPositions();
        }

        bool Brush::hasVertex(const vm::vec3& position, const FloatType epsilon) const {
            ensure(m_geometry != nullptr, "geometry is null");
            return m_geometry->findVertexByPosition(position, epsilon) != nullptr;
        }

        vm::vec3 Brush::findClosestVertexPosition(const vm::vec3& position) const {
            ensure(m_geometry != nullptr, "geometry is null");
            return m_geometry->findClosestVertex(position)->position();
        }

        bool Brush::hasEdge(const vm::segment3& edge, const FloatType epsilon) const {
            ensure(m_geometry != nullptr, "geometry is null");
            return m_geometry->findEdgeByPositions(edge.start(), edge.end(), epsilon) != nullptr;
        }

        bool Brush::hasFace(const vm::polygon3& face, const FloatType epsilon) const {
            ensure(m_geometry != nullptr, "geometry is null");
            return m_geometry->hasFace(face.vertices(), epsilon);
        }

        size_t Brush::edgeCount() const {
            ensure(m_geometry != nullptr, "geometry is null");
            return m_geometry->edgeCount();
        }

        const Brush::EdgeList& Brush::edges() const {
            ensure(m_geometry != nullptr, "geometry is null");
            return m_geometry->edges();
        }

        bool Brush::containsPoint(const vm::vec3& point) const {
            if (!bounds().contains(point)) {
                return false;
            } else {
                for (const auto* face : m_faces) {
                    if (face->boundary().point_status(point) == vm::plane_status::above) {
                        return false;
                    }
                }
                return true;
            }
        }

        std::vector<const BrushFace*> Brush::incidentFaces(const BrushVertex* vertex) const {
            std::vector<const BrushFace*> result;
            result.reserve(m_faces.size());

            auto* first = vertex->leaving();
            auto* current = first;
            do {
                result.push_back(current->face()->payload());
                current = current->nextIncident();
            } while (current != first);

            return result;
        }

        bool Brush::canMoveVertices(const vm::bbox3& worldBounds, const std::vector<vm::vec3>& vertices, const vm::vec3& delta) const {
            return doCanMoveVertices(worldBounds, vertices, delta, true).success;
        }

        std::vector<vm::vec3> Brush::moveVertices(const vm::bbox3& worldBounds, const std::vector<vm::vec3>& vertexPositions, const vm::vec3& delta, const bool uvLock) {
            doMoveVertices(worldBounds, vertexPositions, delta, uvLock);

            // Collect the exact new positions of the moved vertices
            std::vector<vm::vec3> result;
            result.reserve(vertexPositions.size());

            for (const auto& position : vertexPositions) {
                const auto* newVertex = m_geometry->findClosestVertex(position + delta, vm::C::almost_zero());
                if (newVertex != nullptr) {
                    result.push_back(newVertex->position());
                }
            }

            return result;
        }

        bool Brush::canAddVertex(const vm::bbox3& worldBounds, const vm::vec3& position) const {
            ensure(m_geometry != nullptr, "geometry is null");
            return worldBounds.contains(position) && !m_geometry->contains(position);
        }

        BrushVertex* Brush::addVertex(const vm::bbox3& worldBounds, const vm::vec3& position) {
            assert(canAddVertex(worldBounds, position));

            BrushGeometry newGeometry(*m_geometry);
            newGeometry.addPoint(position);

            const PolyhedronMatcher<BrushGeometry> matcher(*m_geometry, newGeometry);
            doSetNewGeometry(worldBounds, matcher, newGeometry);

            auto* newVertex = m_geometry->findClosestVertex(position, vm::C::almost_zero());
            ensure(newVertex != nullptr, "vertex could not be added");
            return newVertex;
        }


        bool Brush::canRemoveVertices(const vm::bbox3& /* worldBounds */, const std::vector<vm::vec3>& vertexPositions) const {
            ensure(m_geometry != nullptr, "geometry is null");
            ensure(!vertexPositions.empty(), "no vertex positions");

            BrushGeometry testGeometry;
            const auto vertexSet = std::set<vm::vec3>(std::begin(vertexPositions), std::end(vertexPositions));

            for (const auto* vertex : m_geometry->vertices()) {
                const auto& position = vertex->position();
                if (!vertexSet.count(position)) {
                    testGeometry.addPoint(position);
                }
            }

            return testGeometry.polyhedron();
        }

        void Brush::removeVertices(const vm::bbox3& worldBounds, const std::vector<vm::vec3>& vertexPositions) {
            ensure(m_geometry != nullptr, "geometry is null");
            ensure(!vertexPositions.empty(), "no vertex positions");
            assert(canRemoveVertices(worldBounds, vertexPositions));

            BrushGeometry newGeometry;
            const auto vertexSet = std::set<vm::vec3>(std::begin(vertexPositions), std::end(vertexPositions));

            for (const auto* vertex : m_geometry->vertices()) {
                const auto& position = vertex->position();
                if (!vertexSet.count(position)) {
                    newGeometry.addPoint(position);
                }
            }

            const PolyhedronMatcher<BrushGeometry> matcher(*m_geometry, newGeometry);
            doSetNewGeometry(worldBounds, matcher, newGeometry);
        }

        bool Brush::canSnapVertices(const vm::bbox3& /* worldBounds */, const FloatType snapToF) const {
            BrushGeometry newGeometry;

            for (const auto* vertex : m_geometry->vertices()) {
                const auto& origin = vertex->position();
                const auto destination = snapToF * round(origin / snapToF);
                newGeometry.addPoint(destination);
            }

            return newGeometry.polyhedron();
        }

        void Brush::snapVertices(const vm::bbox3& worldBounds, const FloatType snapToF, const bool uvLock) {
            ensure(m_geometry != nullptr, "geometry is null");

            BrushGeometry newGeometry;

            for (const auto* vertex : m_geometry->vertices()) {
                const auto& origin = vertex->position();
                const auto destination = snapToF * round(origin / snapToF);
                newGeometry.addPoint(destination);
            }

            using VecMap = std::map<vm::vec3,vm::vec3>;
            VecMap vertexMapping;
            for (const auto* vertex : m_geometry->vertices()) {
                const auto& origin = vertex->position();
                const auto destination = snapToF * round(origin / snapToF);
                if (newGeometry.hasVertex(destination)) {
                    vertexMapping.insert(std::make_pair(origin, destination));
                }
            }

            const PolyhedronMatcher<BrushGeometry> matcher(*m_geometry, newGeometry, vertexMapping);
            doSetNewGeometry(worldBounds, matcher, newGeometry, uvLock);
        }

        bool Brush::canMoveEdges(const vm::bbox3& worldBounds, const std::vector<vm::segment3>& edgePositions, const vm::vec3& delta) const {
            ensure(m_geometry != nullptr, "geometry is null");
            ensure(!edgePositions.empty(), "no edge positions");

            std::vector<vm::vec3> vertexPositions;
            vm::segment3::get_vertices(
                std::begin(edgePositions), std::end(edgePositions),
                std::back_inserter(vertexPositions));
            const auto result = doCanMoveVertices(worldBounds, vertexPositions, delta, false);

            if (!result.success) {
                return false;
            }

            for (const auto& edge : edgePositions) {
                if (!result.geometry->hasEdge(edge.start() + delta, edge.end() + delta)) {
                    return false;
                }
            }

            return true;
        }

        std::vector<vm::segment3> Brush::moveEdges(const vm::bbox3& worldBounds, const std::vector<vm::segment3>& edgePositions, const vm::vec3& delta, const bool uvLock) {
            assert(canMoveEdges(worldBounds, edgePositions, delta));

            std::vector<vm::vec3> vertexPositions;
            vm::segment3::get_vertices(std::begin(edgePositions), std::end(edgePositions),
                                  std::back_inserter(vertexPositions));
            doMoveVertices(worldBounds, vertexPositions, delta, uvLock);

            std::vector<vm::segment3> result;
            result.reserve(edgePositions.size());

            for (const auto& edgePosition : edgePositions) {
                const auto* newEdge = m_geometry->findClosestEdge(edgePosition.start() + delta, edgePosition.end() + delta,
                    vm::C::almost_zero());
                if (newEdge != nullptr) {
                    result.push_back(vm::segment3(newEdge->firstVertex()->position(), newEdge->secondVertex()->position()));
                }
            }

            return result;
        }

        bool Brush::canMoveFaces(const vm::bbox3& worldBounds, const std::vector<vm::polygon3>& facePositions, const vm::vec3& delta) const {
            ensure(m_geometry != nullptr, "geometry is null");
            ensure(!facePositions.empty(), "no face positions");

            std::vector<vm::vec3> vertexPositions;
            vm::polygon3::get_vertices(std::begin(facePositions), std::end(facePositions), std::back_inserter(vertexPositions));
            const auto result = doCanMoveVertices(worldBounds, vertexPositions, delta, false);

            if (!result.success) {
                return false;
            }

            for (const auto& face : facePositions) {
                if (!result.geometry->hasFace(face.vertices() + delta)) {
                    return false;
                }
            }

            return true;
        }

        std::vector<vm::polygon3> Brush::moveFaces(const vm::bbox3& worldBounds, const std::vector<vm::polygon3>& facePositions, const vm::vec3& delta, const bool uvLock) {
            assert(canMoveFaces(worldBounds, facePositions, delta));

            std::vector<vm::vec3> vertexPositions;
            vm::polygon3::get_vertices(std::begin(facePositions), std::end(facePositions), std::back_inserter(vertexPositions));
            doMoveVertices(worldBounds, vertexPositions, delta, uvLock);

            std::vector<vm::polygon3> result;
            result.reserve(facePositions.size());

            for (const auto& facePosition : facePositions) {
                const auto* newFace = m_geometry->findClosestFace(facePosition.vertices() + delta, vm::C::almost_zero());
                if (newFace != nullptr) {
                    result.push_back(vm::polygon3(newFace->vertexPositions()));
                }
            }

            return result;
        }

        Brush::CanMoveVerticesResult::CanMoveVerticesResult(const bool s, BrushGeometry&& g) :
        success(s),
        geometry(std::make_unique<BrushGeometry>(std::move(g))) {}

        Brush::CanMoveVerticesResult Brush::CanMoveVerticesResult::rejectVertexMove() {
            return CanMoveVerticesResult(false, BrushGeometry());
        }

        Brush::CanMoveVerticesResult Brush::CanMoveVerticesResult::acceptVertexMove(BrushGeometry&& result) {
            return CanMoveVerticesResult(true, std::move(result));
        }

        /*
         We determine whether a move is valid by considering the vertices being moved and the vertices
         remaining at their positions as polyhedra. Depending on whether or not they really are polyhedra,
         polygons, edges, points, or empty, we have to consider the following cases.

         REMAINING  || Empty   | Point  | Edge   | Polygon | Polyhedron
         ===========||=========|========|========|=========|============
         MOVING     ||         |        |        |         |
         -----------||---------|--------|--------|---------|------------
         Empty      || n/a     | n/a    | n/a    | n/a     | no
         -----------||---------|--------|--------|---------|------------
         Point      || n/a     | n/a    | n/a    | ok      | check
         -----------||---------|--------|--------|---------|------------
         Edge       || n/a     | n/a    | ok     | check   | check
         -----------||---------|--------|--------|---------|------------
         Polygon    || n/a     | invert | invert | check   | check
         -----------||---------|--------|--------|---------|------------
         Polyhedron || ok      | invert | invert | invert  | check

         n/a    - This case can never occur.
         ok     - This case is always allowed, unless the brush becomes invalid, i.e., not a polyhedron.
         no     - This case is always forbidden.
         invert - This case is handled by swapping the remaining and the moving fragments and inverting the delta. This takes us from a cell at (column, row) to the cell at (row, column).
         check  - Check whether any of the moved vertices would travel through the remaining fragment, or vice versa if inverted case. Also check whether the brush would become invalid, i.e., not a polyhedron.

         If `allowVertexRemoval` is true, vertices can be moved inside a remaining polyhedron.

         */
        Brush::CanMoveVerticesResult Brush::doCanMoveVertices(const vm::bbox3& worldBounds, const std::vector<vm::vec3>& vertexPositions, vm::vec3 delta, const bool allowVertexRemoval) const {
            // Should never occur, takes care of the first row.
            if (vertexPositions.empty() || vm::is_zero(delta, vm::C::almost_zero())) {
                return CanMoveVerticesResult::rejectVertexMove();
            }

            const auto vertexSet = std::set<vm::vec3>(std::begin(vertexPositions), std::end(vertexPositions));

            BrushGeometry remaining;
            BrushGeometry moving;
            BrushGeometry result;
            for (const auto* vertex : m_geometry->vertices()) {
                const auto& position = vertex->position();
                if (!vertexSet.count(position)) {
                    // the vertex is not moving
                    remaining.addPoint(position);
                    result.addPoint(position);
                } else {
                    // the vertex is moving
                    moving.addPoint(position);
                    result.addPoint(position + delta);
                }
            }

            // Will the result go out of world bounds?
            if (!worldBounds.contains(result.bounds())) {
                return CanMoveVerticesResult::rejectVertexMove();
            }

            // Special case, takes care of the first column.
            if (moving.vertexCount() == vertexCount()) {
                return CanMoveVerticesResult::acceptVertexMove(std::move(result));
            }

            // Will vertices be removed?
            if (!allowVertexRemoval) {
                // All moving vertices must still be present in the result
                for (const auto& movingVertex : moving.vertexPositions()) {
                    if (!result.hasVertex(movingVertex + delta)) {
                        return CanMoveVerticesResult::rejectVertexMove();
                    }
                }
            }

            // Will the brush become invalid?
            if (!result.polyhedron()) {
                return CanMoveVerticesResult::rejectVertexMove();
            }

            // One of the remaining two ok cases?
            if ((moving.point() && remaining.polygon()) ||
                (moving.edge() && remaining.edge())) {
                return CanMoveVerticesResult::acceptVertexMove(std::move(result));
            }

            // Invert if necessary.
            if (remaining.point() || remaining.edge() || (remaining.polygon() && moving.polyhedron())) {
                using std::swap;
                swap(remaining, moving);
                delta = -delta;
            }

            // Now check if any of the moving vertices would travel through the remaining fragment and out the other side.
            for (const auto* vertex : moving.vertices()) {
                const auto& oldPos = vertex->position();
                const auto newPos = oldPos + delta;

                for (const auto* face : remaining.faces()) {
                    if (face->pointStatus(oldPos) == vm::plane_status::below &&
                        face->pointStatus(newPos) == vm::plane_status::above) {
                        const auto ray = vm::ray3(oldPos, normalize(newPos - oldPos));
                        const auto distance = face->intersectWithRay(ray, vm::side::back);
                        if (!vm::is_nan(distance)) {
                            return CanMoveVerticesResult::rejectVertexMove();
                        }
                    }
                }
            }

            return CanMoveVerticesResult::acceptVertexMove(std::move(result));
        }

        void Brush::doMoveVertices(const vm::bbox3& worldBounds, const std::vector<vm::vec3>& vertexPositions, const vm::vec3& delta, const bool uvLock) {
            ensure(m_geometry != nullptr, "geometry is null");
            ensure(!vertexPositions.empty(), "no vertex positions");
            assert(canMoveVertices(worldBounds, vertexPositions, delta));

            BrushGeometry newGeometry;
            const auto vertexSet = std::set<vm::vec3>(std::begin(vertexPositions), std::end(vertexPositions));

            for (auto* vertex : m_geometry->vertices()) {
                const auto& position = vertex->position();
                if (vertexSet.count(position)) {
                    newGeometry.addPoint(position + delta);
                } else {
                    newGeometry.addPoint(position);
                }
            }

            using VecMap = std::map<vm::vec3, vm::vec3>;
            VecMap vertexMapping;
            for (auto* oldVertex : m_geometry->vertices()) {
                const auto& oldPosition = oldVertex->position();
                const auto moved = vertexSet.count(oldPosition);
                const auto newPosition = moved ? oldPosition + delta : oldPosition;
                const auto* newVertex = newGeometry.findClosestVertex(newPosition, vm::C::almost_zero());
                if (newVertex != nullptr) {
                    vertexMapping.insert(std::make_pair(oldPosition, newVertex->position()));
                }
            }

            const PolyhedronMatcher<BrushGeometry> matcher(*m_geometry, newGeometry, vertexMapping);
            doSetNewGeometry(worldBounds, matcher, newGeometry, uvLock);
        }

        std::tuple<bool, vm::mat4x4> Brush::findTransformForUVLock(const PolyhedronMatcher<BrushGeometry>& matcher, BrushFaceGeometry* left, BrushFaceGeometry* right) {
            std::vector<vm::vec3> unmovedVerts;
            std::vector<std::pair<vm::vec3, vm::vec3>> movedVerts;

            matcher.visitMatchingVertexPairs(left, right, [&](BrushVertex* leftVertex, BrushVertex* rightVertex){
                const auto leftPosition = leftVertex->position();
                const auto rightPosition = rightVertex->position();

                if (vm::is_equal(leftPosition, rightPosition, vm::constants<FloatType>::almost_zero())) {
                    unmovedVerts.push_back(leftPosition);
                } else {
                    movedVerts.emplace_back(leftPosition, rightPosition);
                }
            });

            // If 3 or more are unmoving, give up.
            // (Picture a square with one corner being moved, we can't possibly lock the UV's of all 4 corners.)
            if (unmovedVerts.size() >= 3) {
                return std::make_tuple(false, vm::mat4x4());
            }

            std::vector<std::pair<vm::vec3, vm::vec3>> referenceVerts;

            // Use unmoving, then moving
            for (const auto& unmovedVert : unmovedVerts) {
                referenceVerts.emplace_back(unmovedVert, unmovedVert);
            }
            // TODO: When there are multiple choices of moving verts (unmovedVerts.size() + movedVerts.size() > 3)
            // we should sort them somehow. This can be seen if you select and move 3/5 verts of a pentagon;
            // which of the 3 moving verts currently gets UV lock is arbitrary.
            kdl::vec_append(referenceVerts, movedVerts);

            if (referenceVerts.size() < 3) {
                // Can't create a transform as there are not enough verts
                return std::make_tuple(false, vm::mat4x4());
            }

            const auto M = vm::points_transformation_matrix(
                    referenceVerts[0].first, referenceVerts[1].first, referenceVerts[2].first,
                    referenceVerts[0].second, referenceVerts[1].second, referenceVerts[2].second);

            if (!(M == M)) {
                // Transform contains nan
                return std::make_tuple(false, vm::mat4x4());
            }

            return std::make_tuple(true, M);
        }

        void Brush::applyUVLock(const PolyhedronMatcher<BrushGeometry>& matcher, BrushFaceGeometry* left, BrushFaceGeometry* right) {
            const auto [success, M] = findTransformForUVLock(matcher, left, right);
            if (!success) {
                return;
            }

            auto* leftFace = left->payload();
            auto* rightFace = right->payload();

            // We want to re-set the texturing of `rightFace` using the texturing from M * leftFace.
            // We don't want to disturb the actual geometry of `rightFace` which is already finalized.
            // So the idea is, clone `leftFace`, transform it by M using texture lock, then copy the texture
            // settings from the transformed clone (which should have an identical plane to `rightFace` within
            // FP error) to `rightFace`.
            auto leftClone = std::unique_ptr<BrushFace>(leftFace->clone());

            try {
                leftClone->transform(M, true);

                auto snapshot = std::unique_ptr<TexCoordSystemSnapshot>(leftClone->takeTexCoordSystemSnapshot());
                rightFace->setAttributes(leftClone->attributes());
                if (snapshot) {
                    // Note, the wrap style doesn't matter because the source and destination faces should have the same plane
                    rightFace->copyTexCoordSystemFromFace(*snapshot, leftClone->attributes().takeSnapshot(),
                                                          leftClone->boundary(), WrapStyle::Rotation);
                }
                rightFace->resetTexCoordSystemCache();
            } catch (const GeometryException&) {
                // do nothing
            }
        }

        void Brush::doSetNewGeometry(const vm::bbox3& worldBounds, const PolyhedronMatcher<BrushGeometry>& matcher, const BrushGeometry& newGeometry, const bool uvLock) {
            matcher.processRightFaces([&](BrushFaceGeometry* left, BrushFaceGeometry* right){
                auto* leftFace = left->payload();
                auto* rightFace = leftFace->clone();

                rightFace->setGeometry(right);
                rightFace->updatePointsFromVertices();

                if (uvLock) {
                    applyUVLock(matcher, left, right);
                }
            });

            kdl::vec_clear_and_delete(m_faces);
            updateFacesFromGeometry(worldBounds, newGeometry);
            rebuildGeometry(worldBounds);
        }

        std::vector<Brush> Brush::subtract(const ModelFactory& factory, const vm::bbox3& worldBounds, const std::string& defaultTextureName, const std::vector<const Brush*>& subtrahends) const {
            auto result = std::vector<BrushGeometry>{*m_geometry};

            for (auto* subtrahend : subtrahends) {
                auto nextResults = std::vector<BrushGeometry>();

                for (const BrushGeometry& fragment : result) {
                    auto subFragments = fragment.subtract(*subtrahend->m_geometry);

                    nextResults.reserve(nextResults.size() + subFragments.size());
                    for (auto& subFragment : subFragments) {
                        nextResults.push_back(std::move(subFragment));
                    }
                }

                result = std::move(nextResults);
            }

            std::vector<Brush> brushes;
            brushes.reserve(result.size());

            for (const auto& geometry : result) {
                try {
                    auto brush = createBrush(factory, worldBounds, defaultTextureName, geometry, subtrahends);
                    brushes.push_back(std::move(brush));
                } catch (const GeometryException&) {}
            }

            return brushes;
        }

        std::vector<Brush> Brush::subtract(const ModelFactory& factory, const vm::bbox3& worldBounds, const std::string& defaultTextureName, const Brush& subtrahend) const {
            return subtract(factory, worldBounds, defaultTextureName, std::vector<const Brush*>{&subtrahend});
        }

        void Brush::intersect(const vm::bbox3& worldBounds, const Brush& brush) {
            for (const auto* face : brush.faces()) {
                addFace(face->clone());
            }

            rebuildGeometry(worldBounds);
        }

        bool Brush::canTransform(const vm::mat4x4& transformation, const vm::bbox3& worldBounds) const {
            try {
                auto testBrush = Brush(*this);
                testBrush.transform(transformation, false, worldBounds);
                return true;
            } catch (GeometryException&) {
                return false;
            }
        }

        void Brush::transform(const vm::mat4x4& transformation, const bool lockTextures, const vm::bbox3& worldBounds) {
            for (auto* face : m_faces) {
                face->transform(transformation, lockTextures);
            }

            rebuildGeometry(worldBounds);
        }

        bool Brush::contains(const vm::bbox3& bounds) const {
            if (!this->bounds().contains(bounds)) {
                return false;
            }

            for (const auto& vertex : bounds.vertices()) {
                if (!containsPoint(vertex)) {
                    return false;
                }
            }

            return true;
        }

        bool Brush::contains(const Brush& brush) const {
            return m_geometry->contains(*brush.m_geometry);
        }

        bool Brush::intersects(const vm::bbox3& bounds) const {
            return this->bounds().intersects(bounds);
        }

        bool Brush::intersects(const Brush& brush) const {
            return m_geometry->intersects(*brush.m_geometry, QueryCallback());
        }

        Brush Brush::createBrush(const ModelFactory& factory, const vm::bbox3& worldBounds, const std::string& defaultTextureName, const BrushGeometry& geometry, const std::vector<const Brush*>& subtrahends) const {
            std::vector<BrushFace*> faces(0);
            faces.reserve(geometry.faceCount());

            for (const auto* face : geometry.faces()) {
                const auto* h1 = face->boundary().front();
                const auto* h0 = h1->next();
                const auto* h2 = h0->next();

                const auto& p0 = h0->origin()->position();
                const auto& p1 = h1->origin()->position();
                const auto& p2 = h2->origin()->position();

                BrushFaceAttributes attribs(defaultTextureName);
                faces.push_back(factory.createFace(p0, p1, p2, attribs));
            }

            auto brush = Brush(worldBounds, faces);
            brush.cloneFaceAttributesFrom(*this);
            for (const auto* subtrahend : subtrahends) {
                brush.cloneInvertedFaceAttributesFrom(*subtrahend);
            }
            return brush;
        }

        void Brush::updateFacesFromGeometry(const vm::bbox3& /* worldBounds */, const BrushGeometry& brushGeometry) {
            m_faces.clear();

            for (const auto* faceG : brushGeometry.faces()) {
                auto* face = faceG->payload();
                if (face != nullptr) { // could happen if the brush isn't fully specified
                    assert(face->geometry() == faceG);
                    addFace(face);
                    face->resetTexCoordSystemCache();
                }
            }
        }

        void Brush::rebuildGeometry(const vm::bbox3& worldBounds) {
            deleteGeometry();
            buildGeometry(worldBounds);
        }

        void Brush::buildGeometry(const vm::bbox3& worldBounds) {
            assert(m_geometry == nullptr);

            m_geometry = new BrushGeometry(worldBounds.expand(1.0));

            AddFacesToGeometry addFacesToGeometry(*m_geometry, m_faces);
            updateFacesFromGeometry(worldBounds, *m_geometry);

            if (addFacesToGeometry.brushEmpty()) {
                throw GeometryException("Brush is empty");
            } else  if (!addFacesToGeometry.brushValid()) {
                throw GeometryException("Brush is invalid");
            } else if (!fullySpecified()) {
                throw GeometryException("Brush is not fully specified");
            }
        }

        void Brush::deleteGeometry() {
            if (m_geometry != nullptr) {
                // clear brush face geometry
                for (auto* brushFace : m_faces) {
                    brushFace->setGeometry(nullptr);
                }
                delete m_geometry;
                m_geometry = nullptr;
            }
        }

        void Brush::findIntegerPlanePoints(const vm::bbox3& worldBounds) {
            for (auto* face : m_faces) {
                face->findIntegerPlanePoints();
            }
            rebuildGeometry(worldBounds);
        }
    }
}
